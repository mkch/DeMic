#define NOMINMAX

#include "framework.h"
#include "ExePluginImpl.h"
#include "plugin.h"
#include "DeMic.h"
#include "Util.h"
#include <codecvt>
#include <algorithm>
#include <sstream>
#include <strsafe.h>

class async {
private:
	template<typename F>
	static void CompletionRoutine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped) {
		auto f = static_cast<F*>(lpOverlapped->hEvent);
		(*f)(dwErrorCode, dwNumberOfBytesTransfered);
		delete f;
	}

	// io should be ReadFileEx or WriteFileEx
	template<typename BUF, typename IO, typename F>
	static BOOL RW(IO io, HANDLE file, BUF buffer, DWORD numBytesToRead, LPOVERLAPPED lpOverlapped, const F& f) {
		lpOverlapped->hEvent = static_cast<HANDLE>(new F(f));
		auto ret = io(file, buffer, numBytesToRead, lpOverlapped, CompletionRoutine<F>);
		if (!ret) {
			delete static_cast<F*>(lpOverlapped->hEvent);
		}
		return ret;
	}

	template<typename BUF, typename IO, typename F>
	static void RWFull(IO io, HANDLE file, BUF buffer, DWORD numBytesToRead, LPOVERLAPPED lpOverlapped, const F& f) {
		class callback {
		public:
			IO io;
			HANDLE file;
			BUF buffer;
			DWORD numBytesToTrans;
			LPOVERLAPPED lpOverlapped;
			F f;
			DWORD numBytesTrans = 0;

			callback(IO io, HANDLE file, BUF buffer, DWORD numBytesToRead, LPOVERLAPPED lpOverlapped, const F& f)
				:io(io), file(file), buffer(buffer), numBytesToTrans(numBytesToRead), lpOverlapped(lpOverlapped), f(f) {}

			void operator()(DWORD err, DWORD n) {
				numBytesTrans += n;
				if (err != 0) {
					f(err, numBytesTrans);
					return;
				}
				if (n < numBytesToTrans) {
					*lpOverlapped = {};
					buffer = ((char*)buffer) + n;
					numBytesToTrans -= n;
					if (!RW(io, file, buffer, numBytesToTrans, lpOverlapped, callback(io, file, buffer, numBytesToTrans, lpOverlapped, f))) {
						f(GetLastError(), numBytesTrans);
					}
					return;
				}
				VERIFY(n == numBytesToTrans);
				f(0, numBytesTrans);
			}
		};
		if (!RW(io, file, buffer, numBytesToRead, lpOverlapped, callback(io, file, buffer, numBytesToRead, lpOverlapped, f))) {
			f(GetLastError(), 0);
		}
	}
public:
	// ReadFull reads exact numBytesToRead bytes of data into buffer from the file.
	// The signature of callback f should be equivalent to the following:
	//   void f(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered);
	template<typename F>
	static void ReadFull(HANDLE file, LPVOID buffer, DWORD numBytesToRead, LPOVERLAPPED lpOverlapped, const F& f) {
		RWFull<LPVOID>(ReadFileEx, file, buffer, numBytesToRead, lpOverlapped, f);
	}

	// WriteFull write exact numBytesToWrite bytes of data in buffer to file.
	// The signature of callback f should be equivalent to the following:
	//   void f(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered);
	template<typename F>
	static void WriteFull(HANDLE file, LPCVOID buffer, DWORD numBytesToWrite, LPOVERLAPPED lpOverlapped, const F& f) {
		RWFull<LPCVOID>(WriteFileEx, file, buffer, numBytesToWrite, lpOverlapped, f);
	}
};

class ReadMessageCtx {
public:
	ReadMessageCtx() {}
	ReadMessageCtx(const ReadMessageCtx&) = delete;
	void Reset() {
		len = 0;
		buf = NULL;
		overlapped = {};
	}
private:
	unsigned short len = 0;
	std::unique_ptr<char[]> buf;
	OVERLAPPED overlapped = {};

	template<typename F>
	friend void ReadMessage(HANDLE file, ReadMessageCtx* ctx, F f);
};

// ReadMessage reads a DeMic plugin2 message.
// The signature of callback f should be equivalent to the following:
//    void f(DWORD deErrorCode, const json& message);
template<typename F>
void ReadMessage(HANDLE file, ReadMessageCtx* ctx, F f) {
	async::ReadFull(file, &ctx->len, sizeof(ctx->len), &ctx->overlapped, [file, ctx, f](auto err, auto n) {
		if (err) {
			f(err, json());
			return;
		}
		ctx->buf = std::make_unique<char[]>(ctx->len);
		ctx->overlapped = {};
		async::ReadFull(file, ctx->buf.get(), ctx->len, &ctx->overlapped, [ctx, f](auto err, auto n) {
			if (err) {
				f(err, json());
				return;
			}
			try {
				f(0, json::parse(std::string(ctx->buf.get(), ctx->len)));
			}
			catch (json::exception) {
				f(0xFFFFFFFF, json());
			}
			});
		});
}

class WriteMessageCtx {
public:
	WriteMessageCtx() = default;
	WriteMessageCtx(const WriteMessageCtx&) = delete;
	~WriteMessageCtx() {
		delete[] buffer;
	}
private:
	void Reset(const std::string& msg) {
		if (buffer) {
			delete[] buffer;
		}
		VERIFY(msg.length() < 0xFF);
		bufferLen = (unsigned short)msg.length();
		buffer = new char[msg.length()];
		memcpy(buffer, &msg[0], msg.length());
		overlapped = {};
	}
	unsigned short bufferLen = 0;
	char* buffer = NULL;
	OVERLAPPED overlapped = {};

	template<typename F>
	friend void WriteMessage(HANDLE file, WriteMessageCtx* ctx, const json& message, F f);
};

template<typename F>
void WriteMessage(HANDLE file, WriteMessageCtx* ctx, const json& message, F f) {
	auto content = (std::ostringstream() << message).str();
	if (content.empty()) {
		VERIFY(FALSE);
		return;
	}
	ctx->Reset(content);
	async::WriteFull(file, &ctx->bufferLen, sizeof(ctx->bufferLen), &ctx->overlapped, [ctx, file, f](auto err, auto n) {
		if (err) {
			f(err, n);
			return;
		}
		async::WriteFull(file, ctx->buffer, ctx->bufferLen, &ctx->overlapped, [f](auto err, auto n) {
			f(err, n);
			});
		});
}

const static wchar_t* const PLUGIN2_EXT = L"plugin2";
static const DWORD DEMIC_CURRENT_SDK2_VERSION = 1;

static std::wstring_convert<std::codecvt_utf8<wchar_t>> wstrconv;

static BOOL CreatePlugin2Process(const std::wstring& path, LPSTARTUPINFO si, LPPROCESS_INFORMATION pi) {
	auto cmdLineBuf = DupCStr(L"\"" + path + L"\" " + L"DeMicPlugin");
	return CreateProcess(
		path.c_str(),			// aplication name
		&cmdLineBuf[0],			// command line 
		NULL,					// process security attributes 
		NULL,					// primary thread security attributes 
		TRUE,					// handles are inherited 
		0,						// creation flags 
		NULL,					// use parent's environment 
		NULL,					// use parent's current directory 
		si,						// STARTUPINFO pointer 
		pi						// receives PROCESS_INFORMATION 
	);
}

BOOL CreateOverlappedPipe(LPHANDLE rd, LPHANDLE wr, LPSECURITY_ATTRIBUTES sa, DWORD nSize) {
	const auto pipeName = L"\\\\.\\pipe\\DeMicPlugin";
	HANDLE server = CreateNamedPipe(pipeName,
		PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES,
		nSize, nSize,
		0,
		sa);
	if (server == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	OVERLAPPED overlapped = { 0 };
	ConnectNamedPipe(server, &overlapped);

	HANDLE client = CreateFile(pipeName, GENERIC_WRITE,
		0,
		sa,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		NULL);
	if (client == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	while (!HasOverlappedIoCompleted(&overlapped)) {
		SleepEx(INFINITE, TRUE);
	}

	*rd = server;
	*wr = client;

	return TRUE;
}


int GetPlugin2DisplayName(const std::wstring& path, const json& info, std::wstring& displayName);

int ReadHelloMessage(const json& msg, const std::wstring& path, std::wstring& displayName) {
	try {
		if (msg["Type"] != "call" || msg["Func"] != "Hello") {
			return -2;
		}
		return GetPlugin2DisplayName(path, msg["Params"], displayName);
	}
	catch (json::exception) {
		return -2;
	}
}

HANDLE LoadPlugin2Info(const std::wstring& path, std::vector<std::unique_ptr<PluginName>>& info) {
	SECURITY_ATTRIBUTES pipeSa = { sizeof(pipeSa), NULL, TRUE };
	HANDLE stdOutRd = NULL, stdOutWr = NULL;
	if (!CreateOverlappedPipe(&stdOutRd, &stdOutWr, &pipeSa, 0)) {
		SHOW_LAST_ERROR();
		return NULL;
	}
	if (!SetHandleInformation(stdOutRd, HANDLE_FLAG_INHERIT, 0)) {
		SHOW_LAST_ERROR();
		CloseHandle(stdOutRd);
		CloseHandle(stdOutWr);
		return NULL;
	}

	STARTUPINFO si = { sizeof(si), 0 };
	si.hStdOutput = stdOutWr;
	si.dwFlags = STARTF_USESTDHANDLES;
	PROCESS_INFORMATION pi = { 0 };

	auto cmdLineBuf = DupCStr(L"\"" + path + L"\" " + L"DeMicPlugin");
	if (!CreatePlugin2Process(path, &si, &pi)) {
		CloseHandle(stdOutRd);
		CloseHandle(stdOutWr);
		return NULL;
	}
	CloseHandle(pi.hThread);
	CloseHandle(stdOutWr);

	auto ctx = new ReadMessageCtx();
	ReadMessage(stdOutRd, ctx, [ctx, path, &info, stdOutRd](auto err, const auto& msg) {
		delete ctx;
		CloseHandle(stdOutRd);
		if (err) {
			info.push_back(NULL);
			return;
		}
		std::wstring displayName;
		switch (ReadHelloMessage(msg, path, displayName)) {
		case 0:
			info.push_back(std::make_unique<PluginName>(path, displayName));
			return;
		case -1:
			MessageBoxW(mainWindow, (path + L"\nA heigher SDK version is required. Please update DeMic!").c_str(), L"DeMic", MB_ICONERROR);
			break;
		}
		info.push_back(NULL);
		});
	return pi.hProcess;
}

static int GetPlugin2DisplayName(const std::wstring& path, const json& info, std::wstring& displayName) {
	try {
		auto sdkVersion = info["SDKVersion"].get<unsigned int>();
		if (sdkVersion > DEMIC_CURRENT_SDK2_VERSION) {
			return -1;
		}
		auto name = wstrconv.from_bytes(info["Name"].get<std::string>());
		auto version = info["Version"];
		auto verMajor = version["Major"].get<unsigned int>();
		auto verMinor = version["Minor"].get<unsigned int>();
		displayName = (std::wstringstream()
			<< name << L" v" << verMajor << L'.' << verMinor
			<< L" (" << GetLastPathComponent(path) << L')'
			).str();
	}
	catch (json::exception) {
		return -2;
	}
	return 0;
}

// Reads all the plugin2s in plugin dir without loading them.
std::vector<PluginName> ReadPluginDir_EXE() {
	std::vector<std::unique_ptr<PluginName>> info;
	std::vector<HANDLE> processes;
	EnumPluginDir(PLUGIN2_EXT, [&info, &processes](auto& path) {
		HANDLE p = LoadPlugin2Info(path, info);
		if (p) {
			processes.push_back(p);
		}
		return true;
		});
	for (auto start = GetTickCount64(); GetTickCount64() - start < 500;) {
		if (SleepEx(10, TRUE) == WAIT_IO_COMPLETION) {
			if (info.size() == processes.size()) {
				break;
			}
		}
	}
	for (auto p : processes) {
		TerminateProcess(p, 0);
		CloseHandle(p);
	}
	std::vector<PluginName> ret;
	for (const auto& i : info) {
		if (i) {
			ret.push_back(*i.get());
		}
	}

	while (info.size() != processes.size()) {
		SleepEx(INFINITE, TRUE);
	}
	return ret;
}

template <typename F>
void Lock(LPCRITICAL_SECTION lock, F f) {
	class Locker {
	public:
		Locker() = delete;
		Locker(const Locker&) = delete;
		Locker(LPCRITICAL_SECTION cs) : Section(cs) {
			EnterCriticalSection(Section);
		}
		~Locker() {
			LeaveCriticalSection(Section);
		}
	private:
		LPCRITICAL_SECTION Section;
	} l(lock);
	f();
}

bool CallPlugin2(const std::wstring& pluginPath, const json& msg, json* ret = nullptr, DWORD timeout = 500);
class ExePlugin;
bool CallPlugin2(const ExePlugin* plugin, json msg, json* ret = nullptr, DWORD timeout = 500);

class PendingReturn {
public:
	PendingReturn() = default;
	PendingReturn(UINT callID, HANDLE event, json* value) :CallID(callID), Event(event), Value(value) {}
	UINT CallID = 0;
	HANDLE Event = nullptr;
	json* Value = nullptr;
};

std::unordered_map<UINT, PendingReturn> pendingReturns;
CRITICAL_SECTION pendingReturnsLock;

static bool AppendToMenu(HMENU hMenu, const json::array_t& items, std::vector<std::pair<UINT, HMENU>>& submenus);

class ExePlugin : public Plugin {
public:
	ExePlugin(const std::wstring displayName, UINT firstMenuItemID, UINT lastMenuItemID,
		const std::wstring& path, HANDLE process, HANDLE stdInWr, HANDLE stdOutRd)
		:Plugin(displayName, firstMenuItemID, lastMenuItemID),
		Path(path), Process(process), StdInWr(stdInWr), StdOutRd(stdOutRd)
	{}

public:
	const std::wstring Path;
	const HANDLE Process;
	const HANDLE StdInWr;
	const HANDLE StdOutRd;
	bool MicMuteStateListenerRegistered = false;
	// The set of dev to operator.
	std::unordered_set<std::wstring> devFilter;
	std::unordered_set<HMENU> InitMenuPopupListeners;

	virtual bool OnLoaded() {
		CallPlugin2(this, {
			{"Type", "call"},
			{"Func", "OnLoaded"},
			{"Params", {
				{"FirstMenuItemID", FirstMenuItemID},
				{"LastMenuItemID", LastMenuItemID},
			}},
			});
		return true; // The real return value is a "return" message.
	}

	virtual bool HasDevFilter() {
		return !devFilter.empty();
	};
	virtual BOOL CallDevFilter(const wchar_t* dev) {
		return devFilter.find(dev) != end(devFilter);
	};
	virtual void CallMicMuteStateListener() {
		if (MicMuteStateListenerRegistered) {
			json msg = {
				{"Type", "call"},
				{"Func", "OnMicMuteStateChanged"},
				{"Param", IsMuted() == TRUE},
			};
			CallPlugin2(this, msg);
		}
	};
	virtual void CallInitMenuPopupListener(HMENU hMenu) {
		if (InitMenuPopupListeners.count(hMenu) == 0) {
			return;
		}
		json ret;
		CallPlugin2(this, {
			{"Type", "call"},
			{"Func", "InitMenuPopupListener"},
			{"Params", uintptr_t(hMenu)},
		}, &ret);

		// The menu item will be added to hMenu by a "return" call.
		for (auto i = GetMenuItemCount(hMenu) - 1; i >= 0; i--) {
			DeleteMenu(hMenu, 0, MF_BYPOSITION); // Delete submenu as well.
		}
		std::vector<std::pair<UINT, HMENU>> submenus;
		if (!AppendToMenu(hMenu, ret, submenus)) {
			for (auto submenu : submenus) {
				DestroyMenu(submenu.second);
			}
			VERIFY(FALSE);
		}
	}
	virtual void CallDefaultDevChangedListener() {
		// TODO:
	}
	virtual void CallOnMenuItemCmd(UINT) {
		// TODO:
	}

	virtual void Unload() {
		CallPlugin2(this, {
			{"Type", "call"},
			{"Func", "Unload"},
			});
	}

	virtual ~ExePlugin() {
		CloseHandle(Process);
		CloseHandle(StdInWr);
		CloseHandle(StdOutRd);
	}
};

static bool AppendToMenu(HMENU hMenu, const json::array_t& items, std::vector<std::pair<UINT, HMENU>>& submenus);

static bool InsertItemToMenu(HMENU hMenu, UINT pos, const json& item, std::vector<std::pair<UINT, HMENU>>& submenus) {
	MENUITEMINFOW info = {};
	info.cbSize = sizeof(info);
	std::vector<wchar_t> strBuf;
	if (item.is_null()) {
		info.fMask = MIIM_TYPE;
		info.fType = MFT_SEPARATOR;
	} else {
		auto id = item.find("ID");
		if (id != end(item)) {
			info.fMask |= MIIM_ID;
			info.wID = *id;
		}
		auto str = item.find("String");
		if (str != end(item)) {
			info.fMask |= MIIM_TYPE;
			info.fType |= MFT_STRING;
			strBuf = DupCStr(wstrconv.from_bytes(*str));
			info.dwTypeData = &strBuf[0];
			info.cch = (UINT)(strBuf.size() - 1);
		}
		auto sub = item.find("Submenu");
		if (sub != end(item)) {
			info.fMask |= MIIM_SUBMENU;
			info.hSubMenu = CreateMenu();
			submenus.push_back(std::make_pair(info.wID, info.hSubMenu));
			if (!AppendToMenu(info.hSubMenu, *sub, submenus)) {
				return false;
			}
		}
	}
	return InsertMenuItem(hMenu, pos, TRUE, &info);
}

static bool AppendToMenu(HMENU hMenu, const json::array_t& items, std::vector<std::pair<UINT, HMENU>>& submenus) {
	for (const auto& item : items) {
		if (!InsertItemToMenu(hMenu, GetMenuItemCount(hMenu), item, submenus)) {
			return false;
		}
	}
	return true;
}

static bool PrependToMenu(HMENU hMenu, const json::array_t& items, std::vector<std::pair<UINT, HMENU>>& submenus) {
	for (auto item = items.crbegin(); item != items.crend(); ++item) {
		if (!InsertItemToMenu(hMenu, 0, *item, submenus)) {
			return false;
		}
	}
	return true;
}

static bool CreateRootMenuItem(ExePlugin* plugin, const json& msg) {
	if (plugin->RootMenuItemCreated) {
		return false;
	}
	auto item = msg["Params"];
	item["ID"] = plugin->FirstMenuItemID;
	std::vector<std::pair<UINT, HMENU>> submenus;
	if (!PrependToMenu(popupMenu, { item, json()}, submenus)) {
		for (auto& submenu : submenus) {
			DestroyMenu(submenu.second);
		}
		return false;
	}

	json ret = {
		{"Type", "return"},
		{"Func", "CreateRootMenuItem"},
		{"Call", (UINT)msg["ID"]},
	};
	json::array_t value;
	for (auto& submenu : submenus) {
		value.push_back({ {"ID", submenu.first}, {"Handle", (uintptr_t)submenu.second} });
	}
	ret["Value"] = value;
	CallPlugin2(plugin, ret);
	plugin->RootMenuItemCreated = true;
	return true;
}

static void HandleFuncCall(const std::wstring& path, const json& msg) {
	std::string func = msg["Func"];
	if (func == "ToggleMuted") {
		ToggleMuted();
	} else if (func == "IsMuted") {
		json reply = {
			{"Type", "return"},
			{"Value", IsMuted() == TRUE},
			{"Func", "IsMuted"},
			{"Call", (UINT)msg["ID"]},
		};
		CallPlugin2(path, reply);
	} else if (func == "RegisterMicMuteStateListener") {
		auto plugin = (ExePlugin*)FindPlugin(path);
		plugin->MicMuteStateListenerRegistered = msg["Param"];
	} else if (func == "CreateRootMenuItem") {
		if (!CreateRootMenuItem((ExePlugin*)FindPlugin(path), msg)) {
			UnloadPlugin(path);
		}
	} else if (func == "RegisterInitMenuPopupListener") {
		auto plugin = (ExePlugin*)FindPlugin(path);
		auto& params = msg["Params"];
		if (params.is_array()) {
			for (uintptr_t menu : params) {
				plugin->InitMenuPopupListeners.insert((HMENU)menu);
			}
		} else {
			plugin->InitMenuPopupListeners.insert((HMENU)(uintptr_t)params);
		}
	}
}

class PluginData {
public:
	PluginData() = default;
	PluginData(const std::wstring& path, HANDLE process, HANDLE stdInWr, HANDLE stdOutRd)
		:Path(path), Process(process), StdInWr(stdInWr), StdOutRd(stdOutRd) {}
	std::wstring Path;
	HANDLE Process = 0;
	HANDLE StdInWr = 0;
	HANDLE StdOutRd = 0;
};

static void HandleFuncReturn(const json& msg) {
	const UINT callID = (UINT)msg["Call"];
	PendingReturn ret;
	Lock(&pendingReturnsLock, [callID, &ret]() {
		auto it = pendingReturns.find(callID);
		if (it == pendingReturns.end()) {
			return;
		}
		ret = it->second;
	});
	*ret.Value = msg["Value"];
	SetEvent(ret.Event);
}

void OnRecvPlugin2Message(const std::wstring& path, const json& msg) {
	try {
		std::string&& t = msg["Type"];
		if (t == "call") {
			HandleFuncCall(path, msg);
		}
	} catch (json::exception) {
		UnloadPlugin(path);
	}
}

void OnPlugin2Dead(const std::wstring& path) {
	UnloadPlugin(path);
	OutputDebugStringW(path.c_str());
	OutputDebugStringA(":");
	OutputDebugStringA(" DEAD!\n");
}

static std::list<PluginData> pluginQueue;
static CRITICAL_SECTION pluginQueueLock = {};
static HANDLE pluginQueueSem = 0;

static std::list<std::pair<HANDLE, json>> writeQueue;
static CRITICAL_SECTION writeQueueLock = {};
static HANDLE writeQueueSem = 0;

static HANDLE pluginThread = 0;


static void HandlePlugin() {
	PluginData plugin;
	Lock(&pluginQueueLock, [&plugin]() {
		plugin = pluginQueue.front();
		pluginQueue.pop_front();
		});
	auto ctx = new ReadMessageCtx();

	class callback {
	public:
		callback(ReadMessageCtx* ctx, const PluginData& plugin) : ctx(ctx), plugin(plugin) {}
		ReadMessageCtx* ctx;
		PluginData plugin;

		void operator() (DWORD err, const json& message) const {
			if (err) {
				Cleanup();
				return;
			}

			bool processed = false;
			try {
				if(message["Type"]=="return"){
					HandleFuncReturn(message);
					processed = true;
				} else {
					SendMessage(mainWindow, UM_RECV_PLUGIN2_MSG, (WPARAM)&plugin.Path, (LPARAM)&message);
				}
			} catch (json::exception) {
				Cleanup();
				return;
			}
			ctx->Reset();
			ReadMessage(plugin.StdOutRd, ctx, callback(ctx, plugin));
 		}
	private:
		void Cleanup() const {
			delete ctx;
			TerminateProcess(plugin.Process, 0);
			SendMessage(mainWindow, UM_PLUGIN2_DEAD, (WPARAM)&plugin.Path, NULL);
		}
	};
	ReadMessage(plugin.StdOutRd, ctx, callback(ctx, plugin));
}

static void HandleWrite() {
	std::pair<HANDLE, json> item;
	Lock(&writeQueueLock, [&item]() {
		item = writeQueue.front();
		writeQueue.pop_front();
		});
	HANDLE stdInWr = item.first;
	const json& message = item.second;

	auto ctx = new WriteMessageCtx();
	WriteMessage(stdInWr, ctx, message, [ctx](auto err, auto n) {
		delete ctx;
	});
}

bool CallPlugin2(const ExePlugin* plugin, json msg, json* ret, DWORD timeout) {
	// So, remember, this function is not thread safe!
	static UINT lastMsgID = 1;
	UINT id = lastMsgID++;
	HANDLE event = nullptr;
	if (ret) {
		event = CreateEvent(nullptr, TRUE, FALSE, NULL);
		VERIFY(event);
		Lock(&pendingReturnsLock, [id, event, ret]() {
			pendingReturns[id] = PendingReturn(id, event, ret);
		});
	}

	msg["ID"] = id;
	Lock(&writeQueueLock, [plugin, &msg]() {
		writeQueue.push_back(std::make_pair(plugin->StdInWr, msg));
	});
	ReleaseSemaphore(writeQueueSem, 1, NULL);

	if (ret) {
		auto wait = WaitForSingleObject(event, timeout);
		CloseHandle(event);
		return wait == WAIT_OBJECT_0;
	}
	return true;
}

bool CallPlugin2(const std::wstring& pluginPath, const json& msg,json* ret, DWORD timeout) {
	auto plugin = (const ExePlugin*)FindPlugin(pluginPath);
	if (!plugin) {
		return false;
	}
	return CallPlugin2(plugin, msg, ret, timeout);
}

static DWORD WINAPI PluginThreadProc(LPVOID lpParameter) {
	HANDLE handles[] = { pluginQueueSem, writeQueueSem };
	while (1) {
		const auto wait = WaitForMultipleObjectsEx(sizeof(handles) / sizeof(handles[0]), handles, false, INFINITE, true);
		if (wait == WAIT_IO_COMPLETION) {
			continue;
		}
		switch (wait) {
		case WAIT_OBJECT_0:
			HandlePlugin();
			break;
		case WAIT_OBJECT_0 + 1:
			HandleWrite();
			break;
		default:
			VERIFY(FALSE);
		}
	}
	return 0;
}

void InitExePlugin() {
	InitializeCriticalSection(&pluginQueueLock);
	pluginQueueSem = CreateSemaphoreW(NULL, 0, std::numeric_limits<LONG>::max(), NULL);
	VERIFY(pluginQueueSem);

	InitializeCriticalSection(&writeQueueLock);
	writeQueueSem = CreateSemaphoreW(NULL, 0, std::numeric_limits<LONG>::max(), NULL);
	VERIFY(writeQueueSem);

	pluginThread = CreateThread(NULL, 0, PluginThreadProc, NULL, 0, NULL);
	VERIFY(pluginThread);

	InitializeCriticalSection(&pendingReturnsLock);
}

void DeinitExePlugin() {
	CloseHandle(pluginQueueSem);
	TerminateThread(pluginThread, 0);
	CloseHandle(pluginThread);
}

std::unique_ptr<Plugin>LoadPlugin_EXE(const std::wstring& path, const std::pair<UINT, UINT>& menuItemIdRange) {
	SECURITY_ATTRIBUTES pipeSa = { sizeof(pipeSa), NULL, TRUE };
	HANDLE stdOutRd = NULL, stdOutWr = NULL;
	if (!CreateOverlappedPipe(&stdOutRd, &stdOutWr, &pipeSa, 0)) {
		SHOW_LAST_ERROR();
		return FALSE;
	}
	if (!SetHandleInformation(stdOutRd, HANDLE_FLAG_INHERIT, 0)) {
		SHOW_LAST_ERROR();
		return FALSE;
	}
	HANDLE stdInRd = NULL, stdInWr = NULL;
	if (!CreateOverlappedPipe(&stdInRd, &stdInWr, &pipeSa, 0)) {
		SHOW_LAST_ERROR();
		return FALSE;
	}
	if (!SetHandleInformation(stdInWr, HANDLE_FLAG_INHERIT, 0)) {
		SHOW_LAST_ERROR();
		return FALSE;
	}

	STARTUPINFO si = { sizeof(si), 0 };
	si.hStdOutput = stdOutWr;
	si.hStdInput = stdInRd;
	si.dwFlags = STARTF_USESTDHANDLES;
	PROCESS_INFORMATION pi = { 0 };

	auto cmdLineBuf = DupCStr(L"\"" + path + L"\" " + L"DeMicPlugin");
	if (!CreatePlugin2Process(path, &si, &pi)) {
		return FALSE;
	}
	CloseHandle(pi.hThread);
	CloseHandle(stdOutWr);
	CloseHandle(stdInRd);

	std::wstring displayName;
	std::unique_ptr<bool> ok;
	ReadMessageCtx ctx;
	ReadMessage(stdOutRd, &ctx, [&ok, &path, &displayName](auto err, const auto& msg) {
		if (err) {
			ok = std::make_unique<bool>(false);
			return;
		}
		if (ReadHelloMessage(msg, path, displayName) != 0) {
			ok = std::make_unique<bool>(false);
			return;
		}
		ok = std::make_unique<bool>(true);
		});


	for (; !ok;) {
		SleepEx(100, true);
	}

	if (!*ok.get()) {
		return NULL;
	}

	Lock(&pluginQueueLock, [&path, pi, stdOutRd, stdInWr]() {
		pluginQueue.push_back(PluginData(path, pi.hProcess, stdInWr, stdOutRd));
	});
	ReleaseSemaphore(pluginQueueSem, 1, NULL);

	return std::make_unique<ExePlugin>(displayName, menuItemIdRange.first, menuItemIdRange.second, path, pi.hProcess, stdInWr, stdOutRd);
}



#include "framework.h"
#include <string>
#include <unordered_map>
#include <algorithm>
#include "Plugin.h"
#include "DeMic.h"
#include "sdk/DeMicPlugin.h"

const static wchar_t* const PLUGIN_DIR_NAME = L"plugin";
const static wchar_t* const PLUGIN_EXT = L"plugin";

struct PluginState;
extern std::unordered_map<std::wstring, PluginState*> loadedPlugins;

void NotifyMicStateChanged();

std::wstring GetPluginDir() {
	static const int BUF_SIZE = 1024;
	wchar_t fileName[BUF_SIZE];
	if (GetModuleFileNameW(NULL, fileName, BUF_SIZE) == BUF_SIZE) {
		DWORD lastError = GetLastError();
		if (lastError) {
			SHOW_ERROR(lastError);
			return L"";
		}
	}

	std::wstring pluginDir(fileName);
	const auto sep = pluginDir.find_last_of(L'\\');
	if (sep != std::wstring::npos) {
		pluginDir = pluginDir.substr(0, sep + 1) + PLUGIN_DIR_NAME;
	}
	return pluginDir;
}

// All successfully loaded plugins.
static std::unordered_map<std::wstring, PluginState*> loadedPlugins;
// All valid plugin file paths.
// Path -> name
static std::unordered_map<std::wstring, std::wstring> validPlugins;

struct PluginState {
	PluginState(const std::wstring& path, HMODULE hDll, DeMic_PluginInfo* pluginInfo, UINT firstMenuItemID, UINT lastMenuItemID) :
		Path(path),
		hModule(hDll),
		PluginInfo(pluginInfo),
		RootMenuItemID(firstMenuItemID),
		FirstMenuItemID(firstMenuItemID), 
		LastMenuItemID(lastMenuItemID) {}
	// File path of this plugin.
	const std::wstring Path;
	const HMODULE hModule;
	const DeMic_PluginInfo* const PluginInfo;
	const UINT RootMenuItemID;
	const UINT FirstMenuItemID;
	const UINT LastMenuItemID;
	bool RootMenuItemCreated = false;
	void (*MicMuteStateListener)() = NULL;
	void (*InitMenuListener)() = NULL;
	void (*DefaultDevChangedListener)() = NULL;
	BOOL (*DevFilter)(const wchar_t*) = NULL;
	std::unordered_map<HMENU, void(*)(HMENU)> InitMenuPopupListeners;
};

extern DeMic_Host host;

// The auto-generated cmd id is started somewhat 32771.
static UINT nextMenuItemID = ID_NO_PLUGIN+1;
// A plugin can create no more than MAX_MENU_ITEM_COUNT menu items.
static const UINT MAX_MENU_ITEM_COUNT = 16;

// File names of selected plugins,
// which will be saved to config file.
std::unordered_set<std::wstring> configuredPluginFiles;
std::unordered_map<UINT, std::wstring> menuCmd2PluginPath;

UINT GetNextPluginMenuCmd() {
	UINT cmd = APS_NextPluginCmdID;
	while(menuCmd2PluginPath.count(cmd)){
		cmd++;
	}
	return cmd;
}

// Gets the last path component of path.
// path must be normalized to use system path separator(\)
static std::wstring GetLastPathComponent(std::wstring path) {
	auto pos = path.find_last_of(L'\\');
	if (pos == std::wstring::npos) {
		return path;
	}
	return path.substr(pos + 1);
}

static std::pair<UINT,UINT> GetFreeMenuItemID() {
	UINT firstID = nextMenuItemID;
	UINT lastID = firstID + MAX_MENU_ITEM_COUNT;
	while (true) {
		if (std::none_of(loadedPlugins.begin(), loadedPlugins.end(),
			[firstID, lastID](const auto& pair) {
				PluginState* state = pair.second;
				return firstID >= state->FirstMenuItemID && firstID <= state->LastMenuItemID
					|| lastID >= state->FirstMenuItemID && lastID <= state->LastMenuItemID;
			})) {
			break; 
		}
		firstID = lastID + 1;
		lastID = firstID + MAX_MENU_ITEM_COUNT;
	}

	return std::pair<UINT, UINT>(firstID, lastID);
}

// Loads a plugin file. Returns true if the plugin is valid,
// false otherwise.
// If the plugin is valid, the HMODULE and the return value
// of GetPluginInfo is set to param info.
// It is the caller's responsibility to call FreeLibrary(info.first);
static bool LoadPluginInfo(const std::wstring& path, std::pair<HMODULE, DeMic_PluginInfo*>& info) {
	HMODULE hModule = LoadLibraryW(path.c_str());
	if (!hModule) {
		SHOW_LAST_ERROR();
		return false;
	}
	const DEMIC_GET_PLUGIN_INFO getPluginInfo = (DEMIC_GET_PLUGIN_INFO)GetProcAddress(hModule, DEMIC_GET_PLUGIN_INFO_FUNC_NAME);
	if (!getPluginInfo) {
		FreeLibrary(hModule);
		return false;
	}
	auto pluginInfo = getPluginInfo();
	if (!pluginInfo) {
		FreeLibrary(hModule);
		return false;
	}
	info.first = hModule;
	info.second = pluginInfo;
	return true;
}

// Loads a plugin if it is not loaded. 
static bool LoadPlugin(const std::wstring& path) {
	std::pair<HMODULE, DeMic_PluginInfo*> info;
	if (!LoadPluginInfo(path, info)) {
		return false;
	}

	const auto file = GetLastPathComponent(path);
	if (info.second->SDKVersion < 1) {
		FreeLibrary(info.first);
		ShowError((path + L"\nInvalid SDK Version!").c_str());
		return false;
	}
	if (info.second->SDKVersion > DEMIC_CURRENT_SDK_VERSION) {
		FreeLibrary(info.first);
		ShowError((path + L"\nRequires a higher SDK Version! Please update DeMic!").c_str());
		return false;
	}

	auto idRange = GetFreeMenuItemID();
	const auto firstID = idRange.first;
	const auto lastID = idRange.second;
	const auto OldNextMenuItemID = nextMenuItemID;
	nextMenuItemID = lastID + 1;
	PluginState* pluginState = new PluginState(path, info.first, info.second, firstID, lastID);

	DeMic_OnLoadedArgs args = { pluginState, pluginState->FirstMenuItemID + 1, pluginState->LastMenuItemID };

	if (!info.second->OnLoaded(&host, &args)) {
		FreeLibrary(info.first);
		delete pluginState;
		nextMenuItemID = OldNextMenuItemID;
		return false;
	}

	loadedPlugins[path] = pluginState;
	return true;
}

template<typename F>
void EnumPluginDir(const F& f) {
	const auto pluginDir = GetPluginDir();
	WIN32_FIND_DATA findData = { 0 };
	HANDLE hFind = FindFirstFileW((pluginDir + L"\\*." + PLUGIN_EXT).c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return;
	}

	while (TRUE) {
		// Load the plugin.
		auto path = pluginDir + L"\\" + findData.cFileName;
		if (!f(path)) {
			break;
		}
		if (!FindNextFileW(hFind, &findData)) {
			DWORD lastError = GetLastError();
			if (lastError) {
				if (lastError == ERROR_NO_MORE_FILES) {
					break;
				}
				SHOW_ERROR(lastError);
				FindClose(hFind);
				return;
			}
		}
	}
	FindClose(hFind);
}

void LoadPlugins() {
	assert(loadedPlugins.empty());
	EnumPluginDir([] (auto& path){
		if (!configuredPluginFiles.count(GetLastPathComponent(path))) {
			return true;
		}
		if (!LoadPlugin(path)) {
			MessageBoxW(mainWindow, (strRes->Load(IDS_CAN_NOT_LOAD_PLUGIN) + path).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
		}
		return true;
	});
}

// Reads all the plugins in plugin dir without loading them.
static std::unordered_map<std::wstring, std::wstring>ReadPluginDir() {
	std::unordered_map<std::wstring, std::wstring> ret;
	EnumPluginDir([&ret](auto& path) {
		std::pair<HMODULE, DeMic_PluginInfo*> info;
		if (LoadPluginInfo(path, info)) {
			ret[path] = info.second->Name;
		}
		FreeLibrary(info.first);
		return true;
	});
	return ret;
}

static BOOL DeletePluginRootMenuItem(PluginState* state) {
	if (!state->RootMenuItemCreated) {
		return FALSE;
	}

	nextMenuItemID = state->FirstMenuItemID;

	for (int i = 0; i < GetMenuItemCount(popupMenu); i++) {
		MENUITEMINFOW info = { sizeof(info), MIIM_ID, 0 };
		if (!GetMenuItemInfoW(popupMenu, i, TRUE, &info)) {
			SHOW_LAST_ERROR();
			return FALSE;
		}
		if (state->RootMenuItemID == info.wID) {
			RemoveMenu(popupMenu, i, MF_BYPOSITION); // Remove the root item.
			RemoveMenu(popupMenu, i, MF_BYPOSITION); // Remove the separator.
			break;
		}
	}
	return TRUE;
}

static void UnloadPlugin(const std::wstring& path) {
	auto it = loadedPlugins.find(path);
	if (it == loadedPlugins.end()) {
		return;
	}
	PluginState* state = it->second;
	loadedPlugins.erase(path);
	FreeLibrary(state->hModule);
	DeletePluginRootMenuItem(state);
	delete state;
}

void OnPluginMenuInitPopup() {
	while (GetMenuItemCount(pluginMenu)) {
		RemoveMenu(pluginMenu, 0, MF_BYPOSITION);
	}
	menuCmd2PluginPath.clear();

	validPlugins = ReadPluginDir();
	if (validPlugins.empty()) {
		VERIFY(AppendMenu(pluginMenu, MF_STRING, ID_NO_PLUGIN, strRes->Load(IDS_NO_PLUGIN).c_str()))
		return;
	}

	std::for_each(validPlugins.begin(), validPlugins.end(), [](const auto& pair) {
		const auto& path = pair.first;
		const auto& name = pair.second;
		const auto id = GetNextPluginMenuCmd();
		VERIFY(AppendMenuW(pluginMenu,
			MF_STRING | (loadedPlugins.count(path) ? MF_CHECKED : 0),
			id,
			(name + L"(" + GetLastPathComponent(path) + L")").c_str()));
		menuCmd2PluginPath[id] = path;
	});

	// Process the renamed loaded plugins(Yes, it's possible to rename a loaded DLL!)
	std::for_each(loadedPlugins.begin(), loadedPlugins.end(), [](const auto& pair) {
		const auto& path = pair.first;
		const std::wstring name = pair.second->PluginInfo->Name;
		const auto id = GetNextPluginMenuCmd();
		if (std::none_of(validPlugins.begin(), validPlugins.end(), [path](const auto pair) { return pair.first == path; })) {
			VERIFY(AppendMenuW(pluginMenu, MF_STRING | MF_CHECKED, id, (name + L"(" + GetLastPathComponent(path) + L") ?").c_str()));
			menuCmd2PluginPath[id] = path; // Path does not exist, but it's OK.
		}
	});
}

void OnPluginMenuItemCmd(UINT cmd) {
	const auto& path = menuCmd2PluginPath[cmd];
	auto const file = GetLastPathComponent(path);
	auto it = configuredPluginFiles.find(file);

	auto load = [path, &file]() {
		if (!LoadPlugin(path)) {
			MessageBoxW(mainWindow, (strRes->Load(IDS_CAN_NOT_LOAD_PLUGIN) + path).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
			configuredPluginFiles.erase(file);
			return false;
		}
		return true;
	};

	if(it != configuredPluginFiles.end()) {
		if (!loadedPlugins.count(path)) {
			if (!load()) {
				return;
			}
		} else {
			configuredPluginFiles.erase(it);
			UnloadPlugin(path);
		}
	} else {
		configuredPluginFiles.insert(file);
		if (!load()) {
			return;
		}
	}
	NotifyMicStateChanged();
	WriteConfig();
}

static void HostTurnOnMic(void* st) {
	TurnOnMic();
}

static void HostTurnOffMic(void* st) {
	TurnOffMic();
}

static void HostToggleMuted(void* st) {
	ToggleMuted();
}


static BOOL HostCreateRootMenuItem(void* st, LPCMENUITEMINFOW lpmi) {
	PluginState* state = (PluginState*)st;
	if (state->RootMenuItemCreated) {
		return FALSE;
	}
	InsertMenu(popupMenu, 0, MF_BYPOSITION | MF_SEPARATOR, NULL, NULL);
	MENUITEMINFOW mi = *lpmi;
	mi.fMask |= MIIM_ID;
	mi.wID = state->RootMenuItemID;
	if (!InsertMenuItemW(popupMenu, 0, true, &mi)) {
		return FALSE;
	}
	state->RootMenuItemCreated = true;
	return TRUE;
}

static BOOL HostModifyRootMenuItem(void* st, LPCMENUITEMINFOW lpmi) {
	const auto state = (PluginState*)st;
	if (!state->RootMenuItemCreated) {
		return FALSE;
	}
	MENUITEMINFOW mi = *lpmi;
	mi.fMask &= ~MIIM_ID; // Menu item id can't be modified.
	return SetMenuItemInfoW(popupMenu, state->RootMenuItemID, FALSE, &mi);
}

static BOOL HostIsMuted() {
	return IsMuted();
}

static void HostSetMicMuteStateListener(void* st, void(*listener)()) {
	PluginState* state = (PluginState*)st;
	state->MicMuteStateListener = listener;
}

void CallPluginMicStateListeners() {
	std::for_each(loadedPlugins.begin(), loadedPlugins.end(),
		[](const auto pair) {
			const auto plugin = pair.second;
		if (plugin->MicMuteStateListener) {
			plugin->MicMuteStateListener();
		}
	});
}

static void HostGetActiveDevices(void (*callback)(const wchar_t* devID, void* userData), void* userData) {
	const auto devices = micCtrl.GetActiveDevices();
	std::for_each(devices.begin(), devices.end(), [callback, userData](const auto id) {
		callback(id.c_str(), userData);
	});
}

static void HostGetDevName(const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData) {
	callback(micCtrl.GetDevName(devID).c_str(), userData);
}

static void HostGetDevIfaceName (const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData) {
	callback(micCtrl.GetDevIfaceName(devID).c_str(), userData);
}

static BOOL HostGetDevMuted(const wchar_t* devID) {
	return micCtrl.GetDevMuted(devID);
}

static void HostSetDevFilter(void* st, BOOL(*filter)(const wchar_t* devID)) {
	((PluginState*)st)->DevFilter = filter;
}

BOOL CallPluginDevFilter(const wchar_t* devID) {
	BOOL result = TRUE;
	for (auto it = loadedPlugins.begin(); it != loadedPlugins.end(); it++) {
		const auto plugin = it->second;
		if (plugin->DevFilter) {
			result = plugin->DevFilter(devID);
			break;
		}
	}
	return result;
}

static void HostSetInitMenuPopupListener(void* st, HMENU menu, void(*listener)(HMENU menu)) {
	auto state = (PluginState*)st;
	if (listener) {
		state->InitMenuPopupListeners[menu] = listener;
	}
	else {
		state->InitMenuPopupListeners.erase(menu);
	}
}

void CallPluginInitMenuPopupListener(HMENU menu) {
	std::for_each(loadedPlugins.begin(), loadedPlugins.end(),
		[menu](const auto pair) {
			const auto plugin = pair.second;
	const auto it = plugin->InitMenuPopupListeners.find(menu);
	if (it != plugin->InitMenuPopupListeners.end()) {
		it->second(menu);
	}
		});
}

void NotifyMicStateChanged() {
	PostMessage(mainWindow, MicCtrl::WM_MUTED_STATE_CHANGED, 0, 0);
}

static void HostNotifyMicStateChanged() {
	NotifyMicStateChanged();
}

static void HostGetDefaultDevID (void(*callback)(const wchar_t* devID, void* userData), void* userData) {
	callback(micCtrl.GetDefaultMicphone().c_str(), userData);
}

static void HostSetDefaultDevChangedListener(void* st, void(*listener)()) {
	auto state = (PluginState*)st;
	state->DefaultDevChangedListener = listener;
}

void CallPluginDefaultDevChangedListeners() {
	std::for_each(loadedPlugins.begin(), loadedPlugins.end(),
		[](const auto pair) {
			const auto plugin = pair.second;
		if (plugin->DefaultDevChangedListener) {
			plugin->DefaultDevChangedListener();
		}
	});
}

static BOOL HostDeleteRootMenuItem(void* st) {
	return DeletePluginRootMenuItem((PluginState*)st);
}

bool ProcessPluginMenuCmd(UINT id) {;
	std::for_each(loadedPlugins.begin(), loadedPlugins.end(), [id](const std::pair<std::wstring, const PluginState*> plugin) {
		const auto state = plugin.second;
		if (id >= state->FirstMenuItemID && id <= state->LastMenuItemID) {
			state->PluginInfo->OnMenuItemCmd(id == state->FirstMenuItemID ? 0 : id);
		}
	});

	return false;
}


static DeMic_Host host = {
	HostCreateRootMenuItem,
	HostTurnOnMic,
	HostTurnOffMic,
	HostToggleMuted,
	HostIsMuted,
	HostSetMicMuteStateListener,
	HostModifyRootMenuItem,
	HostGetActiveDevices,
	HostGetDevName,
	HostGetDevIfaceName,
	HostGetDevMuted,
	HostSetDevFilter,
	HostSetInitMenuPopupListener,
	HostNotifyMicStateChanged,
	HostGetDefaultDevID,
	HostSetDefaultDevChangedListener,
	HostDeleteRootMenuItem,
};
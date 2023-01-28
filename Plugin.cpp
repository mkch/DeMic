#include "framework.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "Plugin.h"
#include "DeMic.h"
#include "Util.h"
#include "sdk/DeMicPlugin.h"

const static wchar_t* const PLUGIN_DIR_NAME = L"plugin";
const static wchar_t* const PLUGIN_EXT = L"plugin";

struct PluginState;
extern std::unordered_map<std::wstring, PluginState*> plugins;

bool LoadPlugin(const std::wstring& path);
void NotifyMicStateChanged();

void LoadPlugins() {
	static const int BUF_SIZE = 1024;
	wchar_t fileName[BUF_SIZE];
	if (GetModuleFileNameW(NULL, fileName, BUF_SIZE) == BUF_SIZE) {
		DWORD lastError = GetLastError();
		if (lastError) {
			SHOW_ERROR(lastError);
			return;
		}
	}

	std::wstring plugInDir(fileName);
	const auto sep = plugInDir.find_last_of(L'\\');
	if (sep != std::wstring::npos) {
		plugInDir = plugInDir.substr(0, sep + 1) + PLUGIN_DIR_NAME;
	}

	WIN32_FIND_DATA findData = { 0 };
	HANDLE hFind = FindFirstFileW((plugInDir + L"\\*." + PLUGIN_EXT).c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return;
	}

	bool pluginLoaded = false;
	while(TRUE) {
		// Load the plugin.
		if (LoadPlugin(plugInDir + L"\\" + findData.cFileName)) {
			pluginLoaded = true;
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

	if (pluginLoaded) {
		MENUITEMINFOW menuInfo = { sizeof(menuInfo), MIIM_STATE, 0, MFS_ENABLED, 0, NULL };
		if (!SetMenuItemInfoW(popupMenu, ID_MENU_PLUGIN, FALSE, &menuInfo)) {
			SHOW_LAST_ERROR();
		}
	}
}

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

static std::unordered_map<std::wstring, PluginState*> plugins;

extern DeMic_Host host;

// The auto-generated cmd id is started somewhat 32771.
static UINT NextMenuItemID = 100;
// A plugin can create no more than MAX_MENU_ITEM_COUNT menu items.
static UINT MAX_MENU_ITEM_COUNT = 16;

std::unordered_set<std::wstring> configuredPluginFiles;
std::unordered_map<UINT, std::wstring> menuCmd2PluginPath;

UINT NextPluginMenuCmd() {
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

bool LoadPlugin(const std::wstring& path) {
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

	std::wstring name = pluginInfo->Name;
	UINT menuItemFlags = MF_STRING;
	const auto file = GetLastPathComponent(path);
	if (!configuredPluginFiles.count(file)) {
		FreeLibrary(hModule);
		pluginInfo = NULL;
		hModule = NULL;
	}else {
		if (pluginInfo->SDKVersion < 1) {
			FreeLibrary(hModule);
			ShowError((path + L"\nInvalid SDK Version!").c_str());
			return false;
		}
		if (pluginInfo->SDKVersion > DEMIC_CURRENT_SDK_VERSION) {
			FreeLibrary(hModule);
			ShowError((path + L"\nRequires a higher SDK Version! Please update DeMic!").c_str());
			return false;
		}

		const auto firstID = NextMenuItemID;
		const auto lastID = NextMenuItemID + MAX_MENU_ITEM_COUNT;
		const auto OldNextMenuItemID = NextMenuItemID;
		NextMenuItemID = lastID + 1;
		PluginState* pluginState = new PluginState(path, hModule, pluginInfo, firstID, lastID);

		DeMic_OnLoadedArgs args = { pluginState, pluginState->FirstMenuItemID + 1, pluginState->LastMenuItemID };

		if (!pluginInfo->OnLoaded(&host, &args)) {
			FreeLibrary(hModule);
			delete pluginState;
			NextMenuItemID = OldNextMenuItemID;
			return false;
		}

		plugins[path] = pluginState;
		menuItemFlags |= MF_CHECKED;
	}
	if (!std::any_of(menuCmd2PluginPath.begin(), menuCmd2PluginPath.end(), [&path](auto pair) { return pair.second == path; })) {
		const auto menuItemID = NextPluginMenuCmd();
		AppendMenuW(pluginMenu, menuItemFlags, menuItemID, (name+ L"(" + file + L")").c_str());
		menuCmd2PluginPath[menuItemID] = path;
	}
	return true;
}

void UnloadPlugin(const std::wstring& path) {
	auto it = plugins.find(path);
	PluginState* state = it->second;
	plugins.erase(path);
	FreeLibrary(state->hModule);
	const UINT rootMenuItemID = state->RootMenuItemID;
	delete state;

	for (int i = 0; i < GetMenuItemCount(popupMenu); i++) {
		MENUITEMINFOW info = { sizeof(info), MIIM_ID, 0 };
		if (!GetMenuItemInfoW(popupMenu, i, TRUE, &info)) {
			SHOW_LAST_ERROR();
			return;
		}
		if (rootMenuItemID == info.wID) {
			RemoveMenu(popupMenu, i, MF_BYPOSITION); // Remove the root item.
			RemoveMenu(popupMenu, i, MF_BYPOSITION); // Remove the separator.
			break;
		}
	}
}

void OnPluginMenuItemCmd(UINT cmd) {
	const auto& path = menuCmd2PluginPath[cmd];
	auto file = GetLastPathComponent(path);
	auto it = configuredPluginFiles.find(file);
	MENUITEMINFOW menuInfo = { sizeof(menuInfo), MIIM_STATE, 0};
	if(it != configuredPluginFiles.end()) {
		configuredPluginFiles.erase(it);
		UnloadPlugin(path);
	} else {
		configuredPluginFiles.insert(file);
		if(!LoadPlugin(path)) {
			SHOW_ERROR(L"Can't load plugin!");
			return;
		}
		menuInfo.fState = MFS_CHECKED;
	}
	if (!SetMenuItemInfoW(pluginMenu, cmd, FALSE, &menuInfo)) {
		SHOW_LAST_ERROR();
	}

	NotifyMicStateChanged();
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


BOOL HostCreateRootMenuItem(void* st, LPCMENUITEMINFOW lpmi) {
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

BOOL HostModifyRootMenuItem(void* st, LPCMENUITEMINFOW lpmi) {
	const auto state = (PluginState*)st;
	if (!state->RootMenuItemCreated) {
		return FALSE;
	}
	MENUITEMINFOW mi = *lpmi;
	mi.fMask &= ~MIIM_ID; // Menu item id can't be modified.
	return SetMenuItemInfoW(popupMenu, state->RootMenuItemID, FALSE, &mi);
}

BOOL HostIsMuted() {
	return IsMuted();
}

void HostSetMicMuteStateListener(void* st, void(*listener)()) {
	PluginState* state = (PluginState*)st;
	state->MicMuteStateListener = listener;
}

void CallPluginMicStateListeners() {
	std::for_each(plugins.begin(), plugins.end(),
		[](const auto pair) {
			const auto plugin = pair.second;
		if (plugin->MicMuteStateListener) {
			plugin->MicMuteStateListener();
		}
	});
}

void HostGetActiveDevices(void (*callback)(const wchar_t* devID, void* userData), void* userData) {
	const auto devices = micCtrl.GetActiveDevices();
	std::for_each(devices.begin(), devices.end(), [callback, userData](const auto id) {
		callback(id.c_str(), userData);
	});
}

void HostGetDevName(const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData) {
	callback(micCtrl.GetDevName(devID).c_str(), userData);
}

void HostGetDevIfaceName (const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData) {
	callback(micCtrl.GetDevIfaceName(devID).c_str(), userData);
}

BOOL HostGetDevMuted(const wchar_t* devID) {
	return micCtrl.GetDevMuted(devID);
}

void HostSetDevFilter(void* st, BOOL(*filter)(const wchar_t* devID)) {
	((PluginState*)st)->DevFilter = filter;
}

BOOL CallPluginDevFilter(const wchar_t* devID) {
	BOOL result = TRUE;
	for (auto it = plugins.begin(); it != plugins.end(); it++) {
		const auto plugin = it->second;
		if (plugin->DevFilter) {
			result = plugin->DevFilter(devID);
			break;
		}
	}
	return result;
}

void HostSetInitMenuPopupListener(void* st, HMENU menu, void(*listener)(HMENU menu)) {
	auto state = (PluginState*)st;
	if (listener) {
		state->InitMenuPopupListeners[menu] = listener;
	}
	else {
		state->InitMenuPopupListeners.erase(menu);
	}
}

void CallPluginInitMenuPopupListener(HMENU menu) {
	std::for_each(plugins.begin(), plugins.end(),
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

void HostNotifyMicStateChanged() {
	NotifyMicStateChanged();
}

void HostGetDefaultDevID (void(*callback)(const wchar_t* devID, void* userData), void* userData) {
	callback(micCtrl.GetDefaultMicphone().c_str(), userData);
}

void HostSetDefaultDevChangedListener(void* st, void(*listener)()) {
	auto state = (PluginState*)st;
	state->DefaultDevChangedListener = listener;
}

void CallPluginDefaultDevChangedListeners() {
	std::for_each(plugins.begin(), plugins.end(),
		[](const auto pair) {
			const auto plugin = pair.second;
		if (plugin->DefaultDevChangedListener) {
			plugin->DefaultDevChangedListener();
		}
	});
}

bool ProcessPluginMenuCmd(UINT id) {;
	std::for_each(plugins.begin(), plugins.end(), [id](const std::pair<std::wstring, const PluginState*> plugin) {
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
};
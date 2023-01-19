#include "framework.h"
#include <string>
#include <map>
#include <algorithm>
#include "Plugin.h"
#include "DeMic.h"
#include "sdk/DeMicPlugin.h"

const static wchar_t* const PLUGIN_DIR_NAME = L"plugin";
const static wchar_t* const PLUGIN_EXT = L"plugin";

void LoadPlugin(const std::wstring& path);

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
		DWORD lastError = GetLastError();
		if (lastError) {
			if (lastError == ERROR_FILE_NOT_FOUND) {
				// No match file
				return;
			}
			SHOW_ERROR(lastError);
			return;
		}
	}

	while(TRUE) {
		// Load the plugin.
		LoadPlugin(plugInDir + L"\\" + findData.cFileName);

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

struct PluginState {
	PluginState(UINT firstMenuItemID, UINT lastMenuItemID):
		RootMenuItemID(firstMenuItemID),
		FirstMenuItemID(firstMenuItemID), 
		LastMenuItemID(lastMenuItemID) {}
	// File path of this plugin.
	std::wstring Path;
	bool RootMenuItemCreated = false;
	const DeMic_PluginInfo* Plugin = NULL;
	const UINT RootMenuItemID;
	const UINT FirstMenuItemID;
	const UINT LastMenuItemID;
	DeMic_MicStateListener MicStateListener = NULL;
};

static std::map<std::wstring, PluginState*> plugins;

extern DeMic_Host host;

// The auto-generated cmd id is started somewhat 32771.
static UINT NextMenuItemID = 100;
// A plugin can create no more than MAX_MENU_ITEM_COUNT menu items.
static UINT MAX_MENU_ITEM_COUNT = 16;

void LoadPlugin(const std::wstring& path) {
	HMODULE hModule = LoadLibraryW(path.c_str());
	if (!hModule) {
		SHOW_LAST_ERROR();
		return;
	}
	const DEMIC_GET_PLUGIN_INFO getPluginInfo = (DEMIC_GET_PLUGIN_INFO)GetProcAddress(hModule, DEMIC_GET_PLUGIN_INFO_FUNC_NAME);
	if (!getPluginInfo) {
		FreeLibrary(hModule);
		return;
	}
	const auto plugin = getPluginInfo();
	if (!plugin) {
		FreeLibrary(hModule);
		return;
	}
	if (plugin->SDKVersion < 1) {
		FreeLibrary(hModule);
		ShowError((path + L"\nPlease update SDK!").c_str());
		return;
	}

	const auto firstID = NextMenuItemID;
	const auto lastID = NextMenuItemID + MAX_MENU_ITEM_COUNT;
	NextMenuItemID = lastID + 1;
	// Not deleted (unless OnLoaded failed).
	PluginState* pluginState = new PluginState(firstID, lastID);
	pluginState->Path = path;
	pluginState->Plugin = plugin;

	DeMic_OnLoadedArgs args = { pluginState, pluginState->FirstMenuItemID+1, pluginState->LastMenuItemID };

	if (!plugin->OnLoaded(&host, &args)) {
		FreeLibrary(hModule);
		delete pluginState;
		return;
	}

	plugins[path] = pluginState;
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
	const auto ok = InsertMenuItemW(popupMenu, 0, true, &mi);
	if (ok) {
		state->RootMenuItemCreated = true;
	}
	return ok;
}

BOOL HostIsMuted() {
	return IsMuted();
}

void HostSetMicStateListener(void* st, DeMic_MicStateListener listener) {
	PluginState* state = (PluginState*)st;
	state->MicStateListener = listener;
}

void CallPluginStateListeners() {
	std::for_each(plugins.begin(), plugins.end(),
		[](const auto pair) {
			const auto plugin = pair.second;
			if (plugin->MicStateListener) {
				plugin->MicStateListener();
			}
		});
}


bool ProcessPluginMenuCmd(UINT id) {;
	std::for_each(plugins.begin(), plugins.end(), [id](const std::pair<std::wstring, const PluginState*> plugin) {
		const auto state = plugin.second;
		if (id >= state->FirstMenuItemID && id <= state->LastMenuItemID) {
			state->Plugin->OnMenuItemCmd(id == state->FirstMenuItemID ? 0 : id);
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
	HostSetMicStateListener,
};
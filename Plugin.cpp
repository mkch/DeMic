#include "framework.h"
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include "Plugin.h"
#include "DllPluginImpl.h"
#include "ExePluginImpl.h"
#include "DeMic.h"


const static wchar_t* const PLUGIN_DIR_NAME = L"plugin";

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
static std::unordered_map<std::wstring, std::unique_ptr<Plugin>> loadedPlugins;

Plugin* FindPlugin(const std::wstring& path) {
	const auto it = loadedPlugins.find(path);
	if (it != loadedPlugins.end()) {
		return it->second.get();
	}
	return NULL;
}

// The auto-generated cmd id is started somewhat 32771.
static UINT nextMenuItemID = ID_NO_PLUGIN+1;
// A plugin can create no more than MAX_MENU_ITEM_COUNT menu items.
static const UINT MAX_MENU_ITEM_COUNT = 16;

// File names of selected plugins,
// which will be saved to config file.
std::unordered_set<std::wstring> configuredPluginFiles;
std::unordered_map<UINT, std::wstring> menuCmd2PluginPath;

// File names of plugin2s that need to pause(via MessageBox) after
// their process are created.
std::unordered_set<std::wstring>waitForDebugger;
// File path of plugin that are waiting for debugger.
std::wstring waitingForDebugger;

// Set the enable state of the plugin menu item.
// Debug purpos only.
void EnablePluginMenuItem(const std::wstring& path, bool enable) {
	waitingForDebugger = enable ? L"" : path;
}

static UINT GetNextPluginMenuCmd() {
	UINT cmd = APS_NextPluginCmdID;
	while(menuCmd2PluginPath.count(cmd)){
		cmd++;
	}
	return cmd;
}

static std::pair<UINT,UINT> GetFreeMenuItemID() {
	UINT firstID = nextMenuItemID;
	UINT lastID = firstID + MAX_MENU_ITEM_COUNT;
	while (true) {
		if (std::none_of(loadedPlugins.begin(), loadedPlugins.end(),
			[firstID, lastID](const auto& pair) {
				auto& plugin = pair.second;
				return firstID >= plugin->FirstMenuItemID && firstID <= plugin->LastMenuItemID
					|| lastID >= plugin->FirstMenuItemID && lastID <= plugin->LastMenuItemID;
			})) {
			break; 
		}
		firstID = lastID + 1;
		lastID = firstID + MAX_MENU_ITEM_COUNT;
	}

	return std::pair<UINT, UINT>(firstID, lastID);
}

static inline bool ends_with(const std::wstring & value, const std::wstring& suffix) {
	if (suffix.size() > value.size()) return false;
	return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}


// Loads a plugin if it is not loaded. 
static bool LoadPlugin(const std::wstring& path) {
	auto&& idRange = GetFreeMenuItemID();
	std::unique_ptr<Plugin> plugin;
	if (ends_with(path, L".plugin")) {
		plugin = LoadPlugin_DLL(path, idRange);
	} else if (ends_with(path, L".plugin2")) {
		plugin = LoadPlugin_EXE(path, idRange);
	}
	if (!plugin) {
		return false;
	}
	nextMenuItemID = idRange.second + 1;
	auto p = plugin.get();
	loadedPlugins[path] = std::move(plugin);
	return p->OnLoaded();
}

void LoadPlugins() {
	assert(loadedPlugins.empty());
	EnumPluginDir(L"plugin*", [](auto& path) {
		if (!configuredPluginFiles.count(GetLastPathComponent(path))) {
			return true;
		}
		if (!LoadPlugin(path)) {
			MessageBoxW(mainWindow, (strRes->Load(IDS_CAN_NOT_LOAD_PLUGIN) + path).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
		}
		return true;
	});
}

bool CreateRootMenuItem(Plugin* plugin, LPCMENUITEMINFOW lpmi) {
	if (plugin->RootMenuItemCreated) {
		return false;
	}
	InsertMenu(popupMenu, 0, MF_BYPOSITION | MF_SEPARATOR, NULL, NULL);
	MENUITEMINFOW mi = *lpmi;
	mi.fMask |= MIIM_ID;
	mi.wID = plugin->RootMenuItemID;
	if (!InsertMenuItemW(popupMenu, 0, true, &mi)) {
		return false;
	}
	plugin->RootMenuItemCreated = true;
	return true;
}


bool DeletePluginRootMenuItem(const Plugin* plugin) {
	if (!plugin->RootMenuItemCreated) {
		return false;
	}

	for (int i = 0; i < GetMenuItemCount(popupMenu); i++) {
		MENUITEMINFOW info = { sizeof(info), MIIM_ID, 0 };
		if (!GetMenuItemInfoW(popupMenu, i, TRUE, &info)) {
			SHOW_LAST_ERROR();
			return false;
		}
		if (plugin->RootMenuItemID == info.wID) {
			RemoveMenu(popupMenu, i, MF_BYPOSITION); // Remove the root item.
			RemoveMenu(popupMenu, i, MF_BYPOSITION); // Remove the separator.
			break;
		}
	}
	return true;
}

bool ModifyRootMenuItem(const Plugin* plugin, LPCMENUITEMINFOW lpmi) {
	if (!plugin->RootMenuItemCreated) {
		return false;
	}
	MENUITEMINFOW mi = *lpmi;
	mi.fMask &= ~MIIM_ID; // Menu item id can't be modified.
	return SetMenuItemInfoW(popupMenu, plugin->RootMenuItemID, FALSE, &mi);
}

void UnloadPlugin(const std::wstring& path) {
	auto it = loadedPlugins.find(path);
	if (it == loadedPlugins.end()) {
		return;
	}
	DeletePluginRootMenuItem(it->second.get());
	it->second->Unload();
	loadedPlugins.erase(path);
}

void OnPluginMenuInitPopup() {
	while (GetMenuItemCount(pluginMenu)) {
		RemoveMenu(pluginMenu, 0, MF_BYPOSITION);
	}
	menuCmd2PluginPath.clear();

	auto validPlugins = ReadPluginDir_DLL();
	for (auto&& exePlugin : ReadPluginDir_EXE()) {
		validPlugins.push_back(exePlugin);
	}
	std::sort(begin(validPlugins), end(validPlugins), [](const auto& a, const auto& b) { return a.DisplayName < b.DisplayName; });

	if (validPlugins.empty()) {
		VERIFY(AppendMenu(pluginMenu, MF_STRING, ID_NO_PLUGIN, strRes->Load(IDS_NO_PLUGIN).c_str()))
		return;
	}

	for(const auto& plugin : validPlugins) {
		const auto id = GetNextPluginMenuCmd();
		VERIFY(AppendMenuW(pluginMenu,
			MF_STRING | (loadedPlugins.count(plugin.Path) ? MF_CHECKED : 0) | (plugin.Path == waitingForDebugger ? MF_GRAYED : 0),
			id, plugin.DisplayName.c_str()));
		menuCmd2PluginPath[id] = plugin.Path;
	}

	// Process the renamed loaded plugins(Yes, it's possible to rename a loaded DLL!)
	for(const auto& pair : loadedPlugins) {
		const auto& path = pair.first;
		if (std::none_of(validPlugins.begin(), validPlugins.end(), [&path](const auto pair) { return pair.Path == path; })) {
			const auto id = GetNextPluginMenuCmd();
			VERIFY(AppendMenuW(pluginMenu, MF_STRING | MF_CHECKED | (path == waitingForDebugger ? MF_GRAYED : 0), id, pair.second->DisplayName.c_str()));
			menuCmd2PluginPath[id] = path; // Path does not exist, but it's OK.
		}
	}
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



void CallPluginMicStateListeners() {
	for (auto& pair : loadedPlugins) {
		pair.second->CallMicMuteStateListener();
	}
}


BOOL CallPluginDevFilter(const wchar_t* devID) {
	BOOL result = TRUE;
	for (auto it = loadedPlugins.begin(); it != loadedPlugins.end(); it++) {
		const auto& plugin = it->second;
		if (plugin->HasDevFilter()) {
			result = plugin->CallDevFilter(devID);
			break;
		}
	}
	return result;
}

void CallPluginInitMenuPopupListener(HMENU menu) {
	for (const auto& plugin : loadedPlugins) {
		plugin.second->CallInitMenuPopupListener(menu);
	}
}

void NotifyMicStateChanged() {
	PostMessage(mainWindow, UM_MUTED_STATE_CHANGED, 0, 0);
}

void CallPluginDefaultDevChangedListeners() {
	for (const auto& plugin : loadedPlugins) {
		plugin.second->CallDefaultDevChangedListener();
	}
}

bool ProcessPluginMenuCmd(UINT id) {;
	for(auto& plugin : loadedPlugins) {
		plugin.second->CallOnMenuItemCmd(id == plugin.second->FirstMenuItemID ? 0 : id);
	};

	return false;
}
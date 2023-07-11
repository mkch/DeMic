#pragma once
#include <unordered_set>
#include "Util.h"
#include "PluginImpl.h"

static const UINT ID_NO_PLUGIN = 99;

extern std::unordered_set<std::wstring> configuredPluginFiles;

// Loads all the plugins in plugin dir.
void LoadPlugins();
// Process menu commands of plugins.
// Returns processed or not.
bool ProcessPluginMenuCmd(UINT id);
// Calls all the mic state changed listeners of plugins.
// when microphone state is changed.
void CallPluginMicStateListeners();
// Calls all the init menu popup listeners of plugins.
// NULL menu is the main system tray menu.
void CallPluginInitMenuPopupListener(HMENU menu);
// Calls all the default device changed listeners in plugins.
void CallPluginDefaultDevChangedListeners();
// Calls the first device filter in plugins.
BOOL CallPluginDevFilter(const wchar_t* devID);
// Called when plugin menu item is selected.
void OnPluginMenuItemCmd(UINT cmd);
// Gets the plugin dir.
std::wstring GetPluginDir();
// Called when plugin menu is about to popup;
void OnPluginMenuInitPopup();

void NotifyMicStateChanged();
bool DeletePluginRootMenuItem(const Plugin* plugin);
bool CreateRootMenuItem(Plugin* plugin, LPCMENUITEMINFOW lpmi);
bool ModifyRootMenuItem(const Plugin* plugin, LPCMENUITEMINFOW lpmi);
Plugin* FindPlugin(const std::wstring& path);
void UnloadPlugin(const std::wstring& path);

template<typename F>
void EnumPluginDir(const wchar_t* ext, const F& f) {
	const auto pluginDir = GetPluginDir();
	WIN32_FIND_DATA findData = { 0 };
	HANDLE hFind = FindFirstFileW((pluginDir + L"\\*." + ext).c_str(), &findData);
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

#pragma once
#include <unordered_set>

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

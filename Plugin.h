#pragma once

// Loads all the plugins in plugin dir.
void LoadPlugins();
// Process menu commands of plugins.
// Returns processed or not.
bool ProcessPluginMenuCmd(UINT id);
// Calls all the mic state changed listeners of plugins.
// when microphone state is changed.
void CallPluginMicStateListeners();
// Calls all the init menu listeners of plugins.
void CallPluginInitMenuListeners();
// Calls all the default device changed listeners in plugins.
void CallPluginDefaultDevChangedListeners();
// Calls the first device filter in plugins.
BOOL CallPluginDevFilter(const wchar_t* devID);

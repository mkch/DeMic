#pragma once

// Load all the plugins in plugin dir.
void LoadPlugins();
// Process menu commands of plugins.
// Returns processed or not.
bool ProcessPluginMenuCmd(UINT id);
// Call all the plugin state listeners
// when microphone state is changed.
void CallPluginStateListeners();
// Call all the init menu listeners in plugins.
void CallPluginInitMenuListeners();

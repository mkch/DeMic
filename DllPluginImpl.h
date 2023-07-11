#pragma once
#include "PluginImpl.h"

std::vector<PluginName>ReadPluginDir_DLL();
std::unique_ptr<Plugin>LoadPlugin_DLL(const std::wstring& path, const std::pair<UINT, UINT>& menuItemIdRange);

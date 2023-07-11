#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "PluginImpl.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

void InitExePlugin();
std::vector<PluginName> ReadPluginDir_EXE();
std::unique_ptr<Plugin>LoadPlugin_EXE(const std::wstring& path, const std::pair<UINT, UINT>& menuItemIdRange);
void OnRecvPlugin2Message(const std::wstring& path, const json& message);
void OnPlugin2Dead(const std::wstring& path);


/*
* 
message: {
	"ID": 100,
	"Type": "call",
	"Func": "function_name",
	"Param": params
}

return message: {
	"ID": 100,
	"Type": "return",
	"Value": ret_value,
	"Func": "function_name"
	"Call": func_call_id
}

*/

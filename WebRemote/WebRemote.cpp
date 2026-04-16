#include "pch.h"
#include "resource.h"
#include "../Util.h"
#include "../sdk/DemicPluginUtil.h"

#include "WebRemote.h"
#include "Server.h"
#include "MessageWindow.h"

#include <boost/json.hpp>
#include <fstream>

HINSTANCE hInstance = NULL;
std::filesystem::path moduleFilePath;
std::filesystem::path configFilePath;
StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;
const wchar_t* appTitle = NULL;


static const char* const CONFIG_SERVER_LISTEN_HOST = "ServerListenHost";
static const char* const CONFIG_SERVER_LISTEN_PORT = "ServerListenPort";

struct Configuration {
	std::string ServerListenHost;
    std::string ServerListenPort;
};

static Configuration config;

static void ReadConfig() {
    namespace json = boost::json;
    std::ifstream in(configFilePath);
    if (!in) {
        return;
    }
    try {
        auto configJson = json::parse(in).as_object();
        config.ServerListenHost = configJson[CONFIG_SERVER_LISTEN_HOST].as_string();
        config.ServerListenPort = configJson[CONFIG_SERVER_LISTEN_PORT].as_string();
    } catch (const std::exception&) {
        ShowError(plugin.Name, (strRes->Load(IDS_READ_CONFIG_FAILED) + configFilePath.c_str()).c_str());
    }
}

static void WriteConfig() {
    namespace json = boost::json;
    json::object configJson;
    configJson[CONFIG_SERVER_LISTEN_HOST] = config.ServerListenHost;
    configJson[CONFIG_SERVER_LISTEN_PORT] = config.ServerListenPort;
    std::ofstream out(configFilePath);
    out << std::setw(2) << json::serialize(configJson);
    if (out.fail()) {
        ShowError(plugin.Name, strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        hInstance = hModule;
		moduleFilePath = GetModuleFilePath(hModule);
		configFilePath =moduleFilePath.replace_extension(L".json").wstring();
        strRes = new StringRes(hModule);
        pluginName = DupCStr(strRes->Load(IDS_APP_NAME));
        plugin.Name = &pluginName[0];
        appTitle = plugin.Name;

        if (!CreateMessageWindow()) {
			throw(Win32Error(GetLastError()));
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        if (lpReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        DestroyMessageWindow();
        break;
    }
    return TRUE;
}

DeMic_Host* host = NULL;
void* state = NULL;

static std::wstring formatErrorMessage(UINT resId, std::wstring& message) {
    auto& reason = strRes->Load(resId);
    if (message.empty()) {
        return reason;
    }
    return reason + L" " + message;
}

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;
    
    ReadConfig();

    if(config.ServerListenHost.empty() && config.ServerListenPort.empty()) {
        ShowError(appTitle, strRes->Load(IDS_LACK_CONFIG).c_str());
        return FALSE;
	}
    InitHTTPServer();

    std::wstring errorMessage;
    auto status = StartHTTPServer(config.ServerListenHost, config.ServerListenPort, errorMessage);
    switch (status) {
    case SERVER_INVALID_ADDRESS_FORMAT:
        ShowError(appTitle, formatErrorMessage(IDS_INVALID_ADDRESS_FORMAT, errorMessage).c_str());
        return false;
    case SERVER_INVALID_PORT:
		ShowError(appTitle, formatErrorMessage(IDS_INVALID_PORT, errorMessage).c_str());
        return false;
    case SERVER_RESOLVE_ENDPOINT:
        ShowError(appTitle, formatErrorMessage(IDS_RESOLVE_ENDPOINT, errorMessage).c_str());
        return false;
    case SERVER_BIND_ERROR:
        ShowError(appTitle, formatErrorMessage(IDS_SERVER_BIND_ERROR, errorMessage).c_str());
        return false;
    case SERVER_LISTEN_ERROR:
        ShowError(appTitle, formatErrorMessage(IDS_SERVER_LISTEN_ERROR, errorMessage).c_str());
		return false;
    case SERVER_ERROR:
        ShowError(appTitle, formatErrorMessage(IDS_SERVER_START_ERROR, errorMessage).c_str());
        return false;
    }

    host->SetMicMuteStateListener(state, [] {
        NotifyStateChange(host->IsMuted());
        });
    NotifyStateChange(host->IsMuted());

    return true;
}

static void OnUnload() {
    CancelStateChangeNotifications();
    StopHTTPServer();
    WriteConfig();
}


static DeMic_PluginInfo plugin = {
    DEMIC_CURRENT_SDK_VERSION,
    L"Web Remote",	/*Name*/
    {1, 0},			/*Version*/
    OnLoaded,		/*OnLoaded*/
    NULL,	        /*OnMenuItemCmd*/
	OnUnload,	    /*OnUnload*/

};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
    return &plugin;
}
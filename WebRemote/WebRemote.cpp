#include "pch.h"
#include "resource.h"
#include "../Util.h"
#include "../sdk/DemicPluginUtil.h"

#include "WebRemote.h"
#include "Server.h"
#include "MessageWindow.h"
#include "VerificationCode.h"

#include <boost/json.hpp>
#include <fstream>

HINSTANCE hInstance = NULL;
std::filesystem::path moduleFilePath;
std::filesystem::path configFilePath;
StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;


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
        ShowError(host, state, (strRes->Load(IDS_READ_CONFIG_FAILED) + configFilePath.c_str()).c_str());
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
        ShowError(host, state, strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
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
        break;
    }
    case DLL_PROCESS_DETACH:
        if (lpReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
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

static HMENU subMenu = NULL;
static UINT showVerificationCodeMenuItemId = 0;

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;
    
    ReadConfig();

    if(config.ServerListenHost.empty() && config.ServerListenPort.empty()) {
        ShowError(host, state, strRes->Load(IDS_LACK_CONFIG).c_str());
        return FALSE;
	}
    InitHTTPServer();

    host->SetMicMuteStateListener(state, [] {
        NotifyStateChange(host->IsMuted());
        });
    NotifyStateChange(host->IsMuted());

	subMenu = CreatePopupMenu();
    if (subMenu == NULL) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }

    showVerificationCodeMenuItemId = args->FirstMenuItemID;
    auto showCodeTitle = strRes->Load(IDS_SHOW_VERIFICATION_CODE);
    MENUITEMINFOW showCodeMenuItem = { sizeof(showCodeMenuItem), 0 };
	showCodeMenuItem.fMask = MIIM_STRING | MIIM_ID;
	showCodeMenuItem.dwTypeData = showCodeTitle.data();
	showCodeMenuItem.cch = UINT(showCodeTitle.length());
    showCodeMenuItem.wID = showVerificationCodeMenuItemId;
    if(!InsertMenuItemW(subMenu, showVerificationCodeMenuItemId, FALSE, &showCodeMenuItem)) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }   

    MENUITEMINFOW rootMenuItem = { sizeof(rootMenuItem), 0 };
    rootMenuItem.fMask = MIIM_STRING | MIIM_SUBMENU;
    rootMenuItem.dwTypeData = &pluginName[0];
    rootMenuItem.cch = UINT(pluginName.size() - 1);
    rootMenuItem.hSubMenu = subMenu;
    if (!host->CreateRootMenuItem(state, &rootMenuItem)) {
        LOG_LAST_ERROR(host, state);
		DestroyMenu(subMenu);
        return FALSE;
    }

    if (!CreateMessageWindow()) {
        LOG_LAST_ERROR(host, state);
		host->DeleteRootMenuItem(state);
        DestroyMenu(subMenu);
        return FALSE;
    }

    std::wstring errorMessage;
    auto status = StartHTTPServer(config.ServerListenHost, config.ServerListenPort, errorMessage);
    if(status != SERVER_OK) {
        switch (status) {
        case SERVER_INVALID_ADDRESS_FORMAT:
            ShowError(host, state, formatErrorMessage(IDS_INVALID_ADDRESS_FORMAT, errorMessage).c_str());
            break;
        case SERVER_INVALID_PORT:
            ShowError(host, state, formatErrorMessage(IDS_INVALID_PORT, errorMessage).c_str());
            break;
        case SERVER_RESOLVE_ENDPOINT:
            ShowError(host, state, formatErrorMessage(IDS_RESOLVE_ENDPOINT, errorMessage).c_str());
            break;
        case SERVER_BIND_ERROR:
            ShowError(host, state, formatErrorMessage(IDS_SERVER_BIND_ERROR, errorMessage).c_str());
            break;
        case SERVER_LISTEN_ERROR:
            ShowError(host, state, formatErrorMessage(IDS_SERVER_LISTEN_ERROR, errorMessage).c_str());
            break;
        case SERVER_ERROR:
            ShowError(host, state, formatErrorMessage(IDS_SERVER_START_ERROR, errorMessage).c_str());
            break;
        }
        host->DeleteRootMenuItem(state);
        DestroyMenu(subMenu);
        DestroyMessageWindow();
        return FALSE;
	}
    
    return TRUE;
}

static void OnMenuItemCmd(UINT id) {
    if(id == showVerificationCodeMenuItemId) {
		ShowVerificationCodeDialog();
    }
}

static void OnUnload() {
    StopHTTPServer();
    DestroyVerificationCodeDialog();
    DestroyMessageWindow();
    DestroyMenu(subMenu);
    CancelStateChangeNotifications();
    WriteConfig();
}


static DeMic_PluginInfo plugin = {
    DEMIC_CURRENT_SDK_VERSION,
    L"Web Remote",	/*Name*/
    {1, 0},			/*Version*/
    OnLoaded,		/*OnLoaded*/
    OnMenuItemCmd,	/*OnMenuItemCmd*/
	OnUnload,	    /*OnUnload*/

};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
    return &plugin;
}
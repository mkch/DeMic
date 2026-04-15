#include "pch.h"
#include "resource.h"
#include "../Util.h"
#include "../sdk/DemicPluginUtil.h"

#include "WebRemote.h"
#include "HTTPServer.h"
#include "MessageWindow.h"

HINSTANCE hInstance = NULL;
StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;
const wchar_t* appTitle = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        hInstance = hModule;
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

std::wstring formatErrorMessage(UINT resId, std::wstring& message) {
    auto& reason = strRes->Load(resId);
    if (message.empty()) {
        return reason;
    }
    return reason + L" " + message;
}

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;
    
    InitHTTPServer();

    host->SetMicMuteStateListener(state, [] {
        NotifyStateChange(host->IsMuted());
	});
    NotifyStateChange(host->IsMuted());

    std::wstring errorMessage;
    auto status = StartHTTPServer(L"127.0.0.1:8080", errorMessage);
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
    return true;
}

static void OnUnload() {
    CancelStateChangeNotifications();
    StopHTTPServer();
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
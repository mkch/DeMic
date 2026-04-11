#include "pch.h"
#include "resource.h"
#include "../Util.h"
#include "../sdk/DemicPluginUtil.h"

#include "WebServer.h"
#include "HTTPServer.h"

HINSTANCE hInstance = NULL;
StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        hInstance = hModule;
        strRes = new StringRes(hModule);
        pluginName = DupCStr(strRes->Load(IDS_APP_NAME));
        plugin.Name = &pluginName[0];
        break;
    }
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

DeMic_Host* host = NULL;
void* state = NULL;

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;

    auto ok = StartHTTPServer();

    HOST_LOG(LevelDebug, L"plugin loaded."); // Test logger

    return ok;
}

static void OnUnload() {
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

#include "pch.h"
#include "resource.h"
#include "HotKeyPlus.h"
#include "../sdk/DemicPluginUtil.h"
#include "MessageWindow.h"

HINSTANCE hInstance = NULL;

StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;

static const wchar_t* const CONFIG_FILE_NAME = L"HotKeyPlus.json";
std::wstring configFilePath;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     ){
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        hInstance = hModule;
        strRes = new StringRes(hModule);
        pluginName = DupCStr(strRes->Load(IDS_APP_NAME));
        plugin.Name = &pluginName[0];
        break;
    }
    case DLL_PROCESS_DETACH:
        // Perform any necessary cleanup.
        if (lpReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        break;
    }
    return TRUE;
}

DeMic_Host* demicHost = NULL;
void* demicState = NULL;

static BOOL OnLoaded(DeMic_Host* host, DeMic_OnLoadedArgs* args) {
    demicHost = host;
    demicState = args->State;

    wchar_t configFile[1024] = { 0 };
    const DWORD gmfn = GetModuleFileNameW(hInstance, configFile, sizeof(configFile) / sizeof(configFile[0]));
    VERIFY_SIMPLE(plugin.Name, gmfn > 0 && gmfn < sizeof(configFile) / sizeof(configFile[0]));
    configFilePath = configFile;
    configFilePath = configFilePath.substr(0, configFilePath.rfind(L'\\') + 1) + CONFIG_FILE_NAME;

    if(!CreateMessageWindow()) {
        return FALSE;
	}

    if (!RegisterInitialHotKey(VK_F12, MOD_CONTROL)) {
        DestroyMessageWindow();
        return FALSE;
    }

    return TRUE;
}

static void OnUnload() {
    UnregisterInitialHotKey();
    DestroyMessageWindow();
}

static void OnMenuItemCmd(UINT id) {}

static DeMic_PluginInfo plugin = {
    DEMIC_CURRENT_SDK_VERSION,
    NULL,			/*Name*/
    {1, 0},			/*Version*/
    OnLoaded,		/*OnLoaded*/
    OnMenuItemCmd,	/*OnMenuItemCmd*/
    OnUnload,		/*OnUnload*/
};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
    return &plugin;
}
// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DeMicPlugin.h"
#include "resource.h"
#include "BillboardWnd.h"
#include "Billboard.h"

HINSTANCE hInstance = NULL;

StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> plugInName;

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpvReserved)  // reserved
{
    switch (fdwReason)
    {
	case DLL_PROCESS_ATTACH:
		hInstance = hinstDLL;
		strRes = new StringRes(hinstDLL);
		utilAppName = strRes->Load(IDS_APP_NAME);
		plugInName = DupCStr(utilAppName);
		plugin.Name = &plugInName[0];
        break;
    case DLL_PROCESS_DETACH:
        if (lpvReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        // Perform any necessary cleanup.
		DestroyBillboardWnd();
		if (host && state) {
			host->DeleteRootMenuItem(state);
		}
        break;
    }
    return TRUE;
}

DeMic_Host* host = NULL;
void* state = NULL;

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
	host = h;
	state = args->State;


	MENUITEMINFOW rootMenuItem = {sizeof(rootMenuItem), 0};
	rootMenuItem.fMask = MIIM_STRING;
	auto title = DupCStr(strRes->Load(IDS_OPEN_BILLBOARD));
	rootMenuItem.dwTypeData = &title[0];
	rootMenuItem.cch = UINT(title.size() - 1);
	if (!host->CreateRootMenuItem(state, &rootMenuItem)) {
		return FALSE;
	}
	host->SetMicMuteStateListener(state, InvalidateBillboardWnd);
	BOOL ok = CreateBillboardWnd();
	if (!ok) {
		host->DeleteRootMenuItem(state);
	}
	return ok;
}

static void OnMenuItemCmd(UINT id) {
	switch (id) {
	case 0: // The root menu item.
		ShowBillboardWnd();
		break;
	}
}

static DeMic_PluginInfo plugin = {
	DEMIC_CURRENT_SDK_VERSION,
	NULL,			/*Name*/
	{1, 0},			/*Version*/
	OnLoaded,		/*OnLoaded*/
	OnMenuItemCmd,	/*OnMenuItemCmd*/
};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
	return &plugin;
}


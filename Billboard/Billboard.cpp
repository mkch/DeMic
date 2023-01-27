// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DemicPlugin.h"
#include "resource.h"
#include "BillboardWnd.h"

HINSTANCE hInstance = NULL;

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpvReserved)  // reserved
{
    switch (fdwReason)
    {
	case DLL_PROCESS_ATTACH:
		hInstance = hinstDLL;
		if (!CreateBillboardWnd()) {
			return FALSE;
		}
        break;
    case DLL_PROCESS_DETACH:
        if (lpvReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        // Perform any necessary cleanup.
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
	wchar_t* title = NULL;
	if (LoadStringW(hInstance, IDS_BILLBOARD, (LPWSTR)&title, 0) == 0) {
		return FALSE;
	}
	rootMenuItem.dwTypeData = title;
	rootMenuItem.cch = lstrlenW(title);
	host->CreateRootMenuItem(state, &rootMenuItem);

	host->SetMicMuteStateListener(state, InvalidateBillboardWnd);
	return TRUE;
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
	L"Billboard",	/*Name*/
	OnLoaded,		/*OnLoaded*/
	OnMenuItemCmd,	/*OnMenuItemCmd*/
};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
	return &plugin;
}


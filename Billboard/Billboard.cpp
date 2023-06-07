// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DeMicPlugin.h"
#include "resource.h"
#include "BillboardWnd.h"
#include "Billboard.h"
#include <codecvt>
#include <fstream>
#include "nlohmann/json.hpp"

HINSTANCE hInstance = NULL;

StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> plugInName;

static const wchar_t* const CONFIG_FILE_NAME = L"Billboard.json";
std::wstring configFilePath;

bool alwaysOnTop = false;

void ReadConfig();
void WriteConfig();

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,		// handle to DLL module
    DWORD fdwReason,		// reason for calling function
    LPVOID lpvReserved) {	// reserved

    switch (fdwReason) {
	case DLL_PROCESS_ATTACH: {
		hInstance = hinstDLL;
		strRes = new StringRes(hinstDLL);
		utilAppName = strRes->Load(IDS_APP_NAME);
		plugInName = DupCStr(utilAppName);
		plugin.Name = &plugInName[0];

		wchar_t configFile[1024] = { 0 };
		const DWORD gmfn = GetModuleFileNameW(hInstance, configFile, sizeof(configFile) / sizeof(configFile[0]));
		VERIFY(gmfn > 0 && gmfn < sizeof(configFile) / sizeof(configFile[0]));
		configFilePath = configFile;
		configFilePath = configFilePath.substr(0, configFilePath.rfind(L'\\') + 1) + CONFIG_FILE_NAME;
		ReadConfig();
		break;
	}
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

const static char* const CONFIG_ALWAYS_ON_TOP = "AlwaysOnTop";

static std::wstring_convert<std::codecvt_utf8<wchar_t>> wstrconv;

using json = nlohmann::json;

void ReadConfig() {
	std::ifstream in(configFilePath);
	if (!in) {
		return;
	}
	try {
		json config = json::parse(in);
		alwaysOnTop = config[CONFIG_ALWAYS_ON_TOP];
	}
	catch (json::exception e) {
		ShowError((strRes->Load(IDS_READ_CONFIG_FAILED) + wstrconv.from_bytes(e.what())).c_str());
	}
}

void WriteConfig() {
	json config = { {CONFIG_ALWAYS_ON_TOP, alwaysOnTop} };
	std::ofstream out(configFilePath);
	out << std::setw(2) << config;
	if (out.fail()) {
		ShowError(strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
	}
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
	{1, 1},			/*Version*/
	OnLoaded,		/*OnLoaded*/
	OnMenuItemCmd,	/*OnMenuItemCmd*/
};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
	return &plugin;
}


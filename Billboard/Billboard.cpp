// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DeMicPlugin.h"
#include "resource.h"
#include "BillboardWnd.h"
#include "Billboard.h"
#include <codecvt>
#include <fstream>
#include "../nlohmann/json.hpp"

HINSTANCE hInstance = NULL;

std::unique_ptr<StringRes> strRes;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;

static const wchar_t* const CONFIG_FILE_NAME = L"Billboard.json";
std::wstring configFilePath;

unsigned int configVersion = 0;
static const decltype(configVersion) CURRENT_CONFIG_VERSION = 1;
bool alwaysOnTop = false;
bool hideCaption = false;
WindowRectRatio lastWindowRect = { 0 };

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,		// handle to DLL module
    DWORD fdwReason,		// reason for calling function
    LPVOID lpvReserved) {	// reserved

    switch (fdwReason) {
	case DLL_PROCESS_ATTACH: {
		hInstance = hinstDLL;
		strRes = std::make_unique<StringRes>(hinstDLL);
		pluginName = DupCStr(strRes->Load(IDS_APP_NAME));
		plugin.Name = &pluginName[0];
		break;
	}
    case DLL_PROCESS_DETACH:
		// Perform any necessary cleanup.
        if (lpvReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        break;
    }
    return TRUE;
}

const static char* const CONFIG_VERSION = "Version";
const static char* const CONFIG_ALWAYS_ON_TOP = "AlwaysOnTop";
const static char* const CONFIG_LAST_RECT = "LastRect";
const static char* const CONFIG_LAST_RECT_RATIO = "LastRectRatio";
const static char* const CONFIG_LEFT = "left";
const static char* const CONFIG_TOP = "top";
const static char* const CONFIG_RIGHT = "right";
const static char* const CONFIG_BOTTOM = "bottom";
const static char* const CONFIG_HIDE_CAPTION = "HideCaption";

using json = nlohmann::json;

// Reads a number member of obj. Returns 0 if it is missing or is not a number.
static double ReadNumber(const json& obj, const char* key) {
	const auto it = obj.find(key);
	if (it == obj.end() || !it->is_number()) {
		return 0.0;
	}
	return it->get<double>();
}

void ReadConfig() {
	std::ifstream in(configFilePath);
	if (!in) {
		return;
	}
	try {
		const auto config = json::parse(in);
		const auto version = config.find(CONFIG_VERSION);
		if(version != config.end()) {
			configVersion = version->get<decltype(configVersion)>();
		}
		alwaysOnTop = config.at(CONFIG_ALWAYS_ON_TOP);
		// Version 1 stores the rect as ratios of the virtual screen (LastRectRatio), so that
		// a Billboard without support for it ignores the rect and uses the default position.
		// Versions without a version number store physical pixels (LastRect).
		const auto rect = config.find(configVersion ? CONFIG_LAST_RECT_RATIO : CONFIG_LAST_RECT);
		if (rect != config.end() && rect->is_object()) {
			lastWindowRect = {
				ReadNumber(*rect, CONFIG_LEFT),
				ReadNumber(*rect, CONFIG_TOP),
				ReadNumber(*rect, CONFIG_RIGHT),
				ReadNumber(*rect, CONFIG_BOTTOM) };
		}
		hideCaption = config.value(CONFIG_HIDE_CAPTION, false); // CONFIG_HIDE_CAPTION is added later, so use value() with default.
	} catch (...) {
		ShowError(host, state, strRes->Load(IDS_READ_CONFIG_FAILED).c_str());
	}
}

void WriteConfig() {
	// Save the rect as ratios of the virtual screen, see GetBillboardWndRelativeRect().
	GetBillboardWndRelativeRect(lastWindowRect);
	json config = { 
		{CONFIG_VERSION, CURRENT_CONFIG_VERSION},
		{CONFIG_ALWAYS_ON_TOP, alwaysOnTop},
		{CONFIG_LAST_RECT_RATIO, {
				{CONFIG_LEFT, lastWindowRect.left},
				{CONFIG_TOP, lastWindowRect.top}, 
				{CONFIG_RIGHT, lastWindowRect.right},
				{CONFIG_BOTTOM, lastWindowRect.bottom},
			} 
		},
		{CONFIG_HIDE_CAPTION, hideCaption},
	};
	std::ofstream out(configFilePath);
	out << std::setw(2) << config;
	if (out.fail()) {
		ShowError(host, state, strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
	}
}

DeMic_Host* host = NULL;
void* state = NULL;

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
	host = h;
	state = args->State;

	wchar_t configFile[1024] = { 0 };
	const DWORD gmfn = GetModuleFileNameW(hInstance, configFile, sizeof(configFile) / sizeof(configFile[0]));
	VERIFY_SIMPLE(plugin.Name, gmfn > 0 && gmfn < sizeof(configFile) / sizeof(configFile[0]));
	configFilePath = configFile;
	configFilePath = configFilePath.substr(0, configFilePath.rfind(L'\\') + 1) + CONFIG_FILE_NAME;

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

static void OnUnload() {
	DestroyBillboardWnd(); // We need to trigger WM_DESTROY to save config file.
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
	{1, 6},			/*Version*/
	OnLoaded,		/*OnLoaded*/
	OnMenuItemCmd,	/*OnMenuItemCmd*/
	OnUnload,		/*OnUnload*/
};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
	return &plugin;
}


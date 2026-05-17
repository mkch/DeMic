#include "pch.h"
#include "resource.h"
#include "HotKeyPlus.h"
#include "../sdk/DemicPluginUtil.h"
#include "MessageWindow.h"
#include "SettingsDialog.h"
#include "../nlohmann/json.hpp"
#include <fstream>

HINSTANCE hInstance = NULL;

StringRes* strRes = NULL;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;

static const wchar_t* const CONFIG_FILE_NAME = L"HotKeyPlus.json";
std::wstring configFilePath;

std::vector<std::wstring> hotkeyTypeNames;

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

int GetHotKeyTypeIndex(HOTKEY_TYPE type) {
    int sel = -1;
    for (size_t i = 0; i < sizeof(hotkeyTypes) / sizeof(hotkeyTypes[0]); i++) {
        if (hotkeyTypes[i] == type) {
            sel = (int)i;
            break;
        }
    }
    return sel;
}

Config config;

void ReadConfig() {
    std::ifstream in(configFilePath);
    if (!in) {
        return;
    }
    try {
        using json = nlohmann::json;
        const auto configJson = json::parse(in);
        const std::string type = configJson["Type"];
        if (type == "Toggle") {
           config.Type = TYPE_TOGGLE;
		   config.Hotkey = configJson["Hotkey"];
           config.Hotkey2 = 0;
        } else if(type == "OnOff") {
            config.Type = TYPE_ON_OFF;
            config.Hotkey = configJson["HotkeyOn"];
            config.Hotkey2 = configJson["HotkeyOff"];;
        } else if(type == "PTT") {
            config.Type = TYPE_PTT;
            config.Hotkey = configJson["Hotkey"];
            config.Hotkey2 = 0;
        } else if (type == "PTM") {
            config.Type = TYPE_PTM;
            config.Hotkey = configJson["Hotkey"];
            config.Hotkey2 = 0;
        } else {
            config.Type = TYPE_NONE;
            config.Hotkey = 0;
            config.Hotkey2 = 0;
            ShowError(demicHost, demicState, strRes->Load(IDS_READ_CONFIG_FAILED).c_str());
        }
    } catch (...) {
        ShowError(demicHost, demicState, strRes->Load(IDS_READ_CONFIG_FAILED).c_str());
    }
}

void WriteConfig() {
    using json = nlohmann::json;
    json configJson;
    if(config.Type == TYPE_TOGGLE) {
        configJson["Type"] = "Toggle";
        configJson["Hotkey"] = config.Hotkey;
    } else if(config.Type == TYPE_ON_OFF) {
        configJson["Type"] = "OnOff";
        configJson["HotkeyOn"] = config.Hotkey;
        configJson["HotkeyOff"] = config.Hotkey2;
    } else if(config.Type == TYPE_PTT) {
        configJson["Type"] = "PTT";
        configJson["Hotkey"] = config.Hotkey;
    } else if(config.Type == TYPE_PTM) {
        configJson["Type"] = "PTM";
        configJson["Hotkey"] = config.Hotkey;
    } else {
        // No valid hotkey type, do not save.
        return;
	}
    std::ofstream out(configFilePath);
    out << std::setw(2) << configJson;
    if (out.fail()) {
        ShowError(demicHost, demicState, strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
    }
}

DeMic_Host* demicHost = NULL;
void* demicState = NULL;

static BOOL OnLoaded(DeMic_Host* host, DeMic_OnLoadedArgs* args) {
    demicHost = host;
    demicState = args->State;

    hotkeyTypeNames = {
        strRes->Load(IDS_TYPE_TOGGLE),
        strRes->Load(IDS_TYPE_ON_OFF),
        strRes->Load(IDS_TYPE_PTT),
        strRes->Load(IDS_TYPE_PTM),
    };

    wchar_t configFile[1024] = { 0 };
    const DWORD gmfn = GetModuleFileNameW(hInstance, configFile, sizeof(configFile) / sizeof(configFile[0]));
    VERIFY_SIMPLE(plugin.Name, gmfn > 0 && gmfn < sizeof(configFile) / sizeof(configFile[0]));
    configFilePath = configFile;
    configFilePath = configFilePath.substr(0, configFilePath.rfind(L'\\') + 1) + CONFIG_FILE_NAME;

    MENUITEMINFOW rootMenuItem = { sizeof(rootMenuItem), 0 };
    rootMenuItem.fMask = MIIM_STRING;
    auto title = DupCStr(strRes->Load(IDS_OPEN_SETTINGS));
    rootMenuItem.dwTypeData = &title[0];
    rootMenuItem.cch = UINT(title.size() - 1);
    if (!host->CreateRootMenuItem(demicState, &rootMenuItem)) {
        return FALSE;
    }

    if(!CreateMessageWindow()) {
		host->DeleteRootMenuItem(demicState);
        return FALSE;
	}

    ReadConfig();
    switch (config.Type) {
    case TYPE_TOGGLE:
    case TYPE_PTT:
    case TYPE_PTM: {
            HotKeyControlInfo info;
            info.SetValue(config.Hotkey);
            RegisterHotKey1(host->GetMainWindow(demicState), info);
            break;
        }
    case TYPE_ON_OFF: {
            HotKeyControlInfo infoOn;
            infoOn.SetValue(config.Hotkey);
            HotKeyControlInfo infoOff;
            infoOff.SetValue(config.Hotkey2);
            if (!RegisterHotKey1(host->GetMainWindow(demicState), infoOn)) {
                break;
            }
            if (!RegisterHotKey2(host->GetMainWindow(demicState), infoOff)) {
                UnregisterHotKeys();
                break;
            }
        }
    }

    return TRUE;
}

static void OnUnload() {
    UnregisterHotKeys();
    DestroyMessageWindow();
}

static void OnMenuItemCmd(UINT id) {
    switch (id) {
    case 0:
        if (!SettingsDialog()) {
			ShowError(demicHost, demicState, L"Failed to open settings dialog");
        }
        break;
    }
}

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
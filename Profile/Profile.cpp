// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DeMicPluginUtil.h"
#include "../Util.h"
#include "resource.h"
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <codecvt>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <strsafe.h>
#include <shlwapi.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

DeMic_Host* host = NULL;
void* state = NULL;

static const wchar_t* const CONFIG_FILE_NAME = L"Profile.json";
std::wstring configFilePath;
StringRes* strRes = NULL;
HMENU devicesMenu = NULL;

void ReadConfig();

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     ) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        strRes = new StringRes(hModule);
        pluginName = DupCStr(strRes->Load(IDS_APP_NAME));
        plugin.Name = &pluginName[0];
        
        wchar_t configFile[1024] = { 0 };
        const DWORD gmfn = GetModuleFileNameW(hModule, configFile, sizeof(configFile) / sizeof(configFile[0]));
        VERIFY_SIMPLE(plugin.Name, gmfn > 0 && gmfn < sizeof(configFile) / sizeof(configFile[0]));
        configFilePath = configFile;
        configFilePath = configFilePath.substr(0, configFilePath.rfind(L'\\')+1) + CONFIG_FILE_NAME;
        ReadConfig();
        break;
    }
    case DLL_PROCESS_DETACH:
        if (lpReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        // Perform any necessary cleanup.
        if (devicesMenu) {
            DestroyMenu(devicesMenu);
        }
        if (host && state) {
            host->DeleteRootMenuItem(state);
        }
        break;
    }
    return TRUE;
}

UINT firstMenuItemID = 0;
UINT lastMenuItemID = 0;

void MainMenuPopupListener(HMENU menu);
void SubMenuPopupListener(HMENU menu);
BOOL MicDevFilter(const wchar_t* devID);

void DefaultDevChangedListener() {
    host->NotifyMicStateChanged();
}

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;

    firstMenuItemID = args->FirstMenuItemID;
    lastMenuItemID = args->LastMenuItemID;

    host->SetInitMenuPopupListener(state, NULL, MainMenuPopupListener);
    devicesMenu = CreatePopupMenu();
    host->SetInitMenuPopupListener(state, devicesMenu, SubMenuPopupListener);

    host->SetDevFilter(state, MicDevFilter);
    host->SetDefaultDevChangedListener(state, DefaultDevChangedListener);

    MENUITEMINFOW rootMenuItem = { sizeof(rootMenuItem), 0 };
    rootMenuItem.fMask = MIIM_STRING | MIIM_SUBMENU;
    auto title = DupCStr(strRes->Load(IDS_APPLY_TO_ONE));
    rootMenuItem.dwTypeData = &title[0];
    rootMenuItem.cch = UINT(title.size() - 1);
    rootMenuItem.hSubMenu = devicesMenu;
    VERIFY(host, state, host->CreateRootMenuItem(state, &rootMenuItem));
    return TRUE;
}

UINT allDevMenuID = 0; // Menu item id of "All".
UINT allDevExcpetMenuID = 0; // Menu item id of "All excpet the flollowing".

// Key: Menu item id.
// Value: Microphone device id and device name.
std::unordered_map<UINT, std::pair<std::wstring, std::wstring>> menuID2Dev;


// All the selected microphone devices.
// Key: Device ID; Value: Device name.
// Empyt map: all devices.
std::unordered_map<std::wstring, std::wstring> selectedDev;
// Whether selectedDev is a exclusion list.
bool excludeSelected = false;

const static char* const CONFIG_DEFAULT_PROFILE_NAME = "Default";
const static char* const CONFIG_DEVICES = "Devices";
const static char* const CONFIG_ID = "ID";
const static char* const CONFIG_NAME = "Name";
const static char* const CONFIG_EXCLUDE = "Exclude";

static std::wstring_convert<std::codecvt_utf8<wchar_t>> wstrconv;

void ReadConfig() {
    std::ifstream in(configFilePath);
    if (!in) {
        return;
    }
    try {
        json config = json::parse(in);
        json* devices = NULL;
        auto& profile = config[CONFIG_DEFAULT_PROFILE_NAME];
        if (profile.is_object()) {
            excludeSelected = profile[CONFIG_EXCLUDE];
            devices = &profile[CONFIG_DEVICES];
        } else {
            devices = &profile; // profile is an array of devices.
        }
        std::for_each(devices->begin(), devices->end(), [](const json& dev) {
            auto id = dev[CONFIG_ID].get<std::string>();
            auto name = dev[CONFIG_NAME].get<std::string>();
            selectedDev[wstrconv.from_bytes(id)] = wstrconv.from_bytes(name);
        });
    } catch(...) {
        ShowError(plugin.Name, strRes->Load(IDS_READ_CONFIG_FAILED).c_str());
    }
}

void WriteConfig() {
    auto devices = json::array();
    std::for_each(selectedDev.begin(), selectedDev.end(), [&devices](auto const& dev) {
        devices.push_back({ {CONFIG_ID, wstrconv.to_bytes(dev.first)}, {CONFIG_NAME, wstrconv.to_bytes(dev.second) }});
    });
    json config = { {CONFIG_DEFAULT_PROFILE_NAME,
        {{CONFIG_EXCLUDE, excludeSelected},
            {CONFIG_DEVICES, devices}}
        } };
    std::ofstream out(configFilePath);
    out << std::setw(2) << config;
    if (out.fail()) {
        ShowError(plugin.Name, strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
    }
}

// Dummy device ID for default microphone.
const static wchar_t* DEFAULT_MIC_DEV_ID = L"<DEFAULT_MICROPHONE>";

void IsDefaultDev(const wchar_t* devID, void* userData) {
    auto data = (std::pair<const wchar_t*, int>*)userData;
    data->second = lstrcmpW(devID, data->first);
}

BOOL MicDevFilter(const wchar_t* devID) {
    if (selectedDev.empty()) { // Empty: All devices.
        return TRUE;
    }

    if (excludeSelected) {
        auto userData = std::pair<const wchar_t*, int>(devID, 0);
        host->GetDefaultDevID(IsDefaultDev, &userData);
        const bool isDefault = userData.second == 0;
        for (auto it = selectedDev.begin(); it != selectedDev.end(); ++it) {
            if (it->first == DEFAULT_MIC_DEV_ID && isDefault) {
                return FALSE;
            }
            if (it->first == devID) {
                return FALSE;
            }
        }
        return TRUE;
    } else {
        if (selectedDev.count(DEFAULT_MIC_DEV_ID)) {
            auto userData = std::pair<const wchar_t*, int>(devID, 0);
            host->GetDefaultDevID(IsDefaultDev, &userData);
            return userData.second == 0;
        }
        return selectedDev.find(devID) != selectedDev.end();
    }
}

void DevNameString(const wchar_t* name, void* userData) {
    ((std::wstring*)userData)->assign(name);
}

struct EnumDevProcData {
    // Current menu item id.
    UINT ItemID = 0;
    // Device ids enumerated.
    std::unordered_set<std::wstring> EnumedDevID;
};


// Append a menu item to show "too many microphones".
void AppendTooManyMicrophonesMenuItem(HMENU menu, const std::wstring& title) {
    const static std::wstring TOO_MANY = L"<!!!TOO MANY !!!>";
    VERIFY(host, state, AppendMenu(menu, MF_STRING, 0, (TOO_MANY + L" " + title).c_str()));
}

void EnumDevProc(const wchar_t* devID, void* userData) {
    auto data = (EnumDevProcData*)userData;
    data->EnumedDevID.insert(devID);
    std::wstring name;
    host->GetDevIfaceName(devID, DevNameString, &name);
    UINT flags = MF_STRING;
    if (selectedDev.find(devID) != selectedDev.end()) {
        flags |= MF_CHECKED;
    }
    if (data->ItemID >= lastMenuItemID) {
        AppendTooManyMicrophonesMenuItem(devicesMenu, name);
        return; // No more IDs available.
    }
    VERIFY(host, state, AppendMenu(devicesMenu, flags, data->ItemID, name.c_str()))
    menuID2Dev[data->ItemID] = std::pair<std::wstring, std::wstring>(devID, name);
    data->ItemID++;
}

void MainMenuPopupListener(HMENU menu) {
    // Updates the menu item of this plugin in main menu.
    std::vector<wchar_t> rootTitle;
    if (selectedDev.empty()) {
        rootTitle = DupCStr(strRes->Load(IDS_APPLY_TO_ALL));
    } else if (excludeSelected) {
        rootTitle.assign(255, 0);
        StringCchPrintfW(&rootTitle[0], rootTitle.size(), strRes->Load(IDS_APPLY_TO_ALL_EXCEPT).c_str(), selectedDev.size());
    } else if (selectedDev.size() == 1) {
        if (selectedDev.count(DEFAULT_MIC_DEV_ID)) {
            rootTitle = DupCStr(strRes->Load(IDS_APPLY_TO_DEFAULT));
        }
        else {
            rootTitle = DupCStr(strRes->Load(IDS_APPLY_TO_ONE));
        }
    } else {
        rootTitle.assign(255, 0);
        StringCchPrintfW(&rootTitle[0], rootTitle.size(), strRes->Load(IDS_APPLY_TO).c_str(), selectedDev.size());
    }
    MENUITEMINFOW info = { sizeof(info), 0 };
    info.fMask = MIIM_STRING | MIIM_SUBMENU;
    info.hSubMenu = devicesMenu;
    info.dwTypeData = &rootTitle[0];
    VERIFY(host, state, host->ModifyRootMenuItem(state, &info));
}

void SubMenuPopupListener(HMENU menu) {
    while(GetMenuItemCount(devicesMenu)) {
        RemoveMenu(devicesMenu, 0, MF_BYPOSITION);
    }
    menuID2Dev.clear();

    UINT menuItemID = firstMenuItemID;
    allDevMenuID = menuItemID;
    VERIFY(host, state, AppendMenu(devicesMenu,
        MF_STRING | ((selectedDev.empty() && !excludeSelected) ? MF_CHECKED : 0),
        allDevMenuID, strRes->Load(IDS_ALL_MICROPHONES).c_str()));
    menuItemID++;

    VERIFY(host, state, AppendMenu(devicesMenu, MF_SEPARATOR, 0, NULL));

    allDevExcpetMenuID = menuItemID;
    VERIFY(host, state, AppendMenu(devicesMenu,
        MF_STRING | (excludeSelected ? MF_CHECKED : 0),
        allDevExcpetMenuID, strRes->Load(IDS_ALL_EXCEPT).c_str()));
    menuItemID++;

    VERIFY(host, state, AppendMenu(devicesMenu, MF_SEPARATOR, 0, NULL));

    VERIFY(host, state, AppendMenu(devicesMenu,
        MF_STRING | (selectedDev.size() == 1 && selectedDev.count(DEFAULT_MIC_DEV_ID) ? MF_CHECKED : 0),
        menuItemID, strRes->Load(IDS_DEFAULT_MICROPHONE).c_str()));
    menuID2Dev[menuItemID] = std::pair<std::wstring, std::wstring>(DEFAULT_MIC_DEV_ID, L"");
    menuItemID++;

    VERIFY(host, state, AppendMenu(devicesMenu, MF_SEPARATOR, 0, NULL));

    EnumDevProcData data = { menuItemID };
    host->GetActiveDevices(EnumDevProc, &data);

    // Append extra items in selectedDevID.
    std::for_each(selectedDev.begin(), selectedDev.end(), [&data](const auto& dev) {
        if (dev.first == DEFAULT_MIC_DEV_ID || data.EnumedDevID.find(dev.first) != data.EnumedDevID.end()) {
            return;
        }
        if (data.ItemID >= lastMenuItemID) {
            AppendTooManyMicrophonesMenuItem(devicesMenu, dev.second);
            return; // No more IDs available.
        }
        VERIFY(host, state, AppendMenu(devicesMenu, MF_STRING | MF_CHECKED, data.ItemID, dev.second.c_str()));
        menuID2Dev[data.ItemID] = dev;
        data.ItemID++;
    });
}

static void OnMenuItemCmd(UINT id) {
    if (id == 0) { // The root menu item.
        return;
    }
    if (id == allDevMenuID) {
        selectedDev.clear();
        excludeSelected = false;
    } else if (id == allDevExcpetMenuID) {
        excludeSelected = true;
    } else {
        selectedDev.erase(DEFAULT_MIC_DEV_ID);
        auto dev = menuID2Dev[id];
        if (dev.first == DEFAULT_MIC_DEV_ID) {
            selectedDev.clear();
            selectedDev[dev.first] = dev.second;
        } else if (selectedDev.count(dev.first)) {
            selectedDev.erase(dev.first);
        } else {
            selectedDev[dev.first] = dev.second;
        }
    }
    host->NotifyMicStateChanged();
    WriteConfig();
}

static DeMic_PluginInfo plugin = {
    DEMIC_CURRENT_SDK_VERSION,
    NULL,	        /*Name*/
    {1, 3},			/*Version*/
    OnLoaded,		/*OnLoaded*/
    OnMenuItemCmd,	/*OnMenuItemCmd*/
};

extern "C" DeMic_PluginInfo * GetPluginInfo(void) {
    return &plugin;
}


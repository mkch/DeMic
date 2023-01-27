// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DemicPlugin.h"
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

static const wchar_t* const CONFIG_FILE_NAME = L"Profile.json";
std::wstring configFilePath;

StringRes* strRes = NULL;

void ReadConfig();

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     ) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        strRes = new StringRes(hModule);
        utilAppName = strRes->Load(IDS_APP_NAME);
        
        wchar_t configFile[1024] = { 0 };
        const DWORD gmfn = GetModuleFileNameW(hModule, configFile, sizeof(configFile) / sizeof(configFile[0]));
        VERIFY(gmfn > 0 && gmfn < sizeof(configFile) / sizeof(configFile[0]));
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
        break;
    }
    return TRUE;
}

DeMic_Host* host = NULL;
void* state = NULL;
UINT firstMenuItemID = 0;
UINT lastMenuItemID = 0;

void InitMenuListener();
BOOL MicDevFilter(const wchar_t* devID);

void DefaultDevChangedListener() {
    host->NotifyMicStateChanged();
}

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;
    firstMenuItemID = args->FirstMenuItemID;
    lastMenuItemID = args->LastMenuItemID;

    host->SetInitMenuListener(state, InitMenuListener);
    host->SetDevFilter(MicDevFilter);
    host->SetDefaultDevChangedListener(state, DefaultDevChangedListener);

    MENUITEMINFOW rootMenuItem = { sizeof(rootMenuItem), 0 };
    rootMenuItem.fMask = MIIM_STRING;
    auto title = DupCStr(strRes->Load(IDS_APPLY_TO_ONE));
    rootMenuItem.dwTypeData = &title[0];
    rootMenuItem.cch = UINT(title.size() - 1);
    VERIFY(host->CreateRootMenuItem(state, &rootMenuItem));
    return TRUE;
}

HMENU devicesMenu = NULL;
UINT allDevMenuID = 0; // Menu item id of "All".

// Key: Menu item id.
// Value: Microphone device id and device name.
std::unordered_map<UINT, std::pair<std::wstring, std::wstring>> menuID2Dev;


// All the selected microphone devices.
// Key: Device ID; Value: Device name.
// Empyt map: all devices.
std::unordered_map<std::wstring, std::wstring> selectedDev;

const static char* const CONFIG_DEFAULT_PROFILE_NAME = "Default";
const static char* const CONFIG_DEVICES = "Devices";
const static char* const CONFIG_ID = "ID";
const static char* const CONFIG_NAME = "Name";

static std::wstring_convert<std::codecvt_utf8<wchar_t>> wstrconv;

void ReadConfig() {
    std::ifstream in(configFilePath);
    if (!in) {
        return;
    }
    try {
        json config = json::parse(in);
        auto& devices = config[CONFIG_DEFAULT_PROFILE_NAME];
        for_each(devices.begin(), devices.end(), [](const json& dev) {
            auto id = dev[CONFIG_ID].get<std::string>();
            auto name = dev[CONFIG_NAME].get<std::string>();
            selectedDev[wstrconv.from_bytes(id)] = wstrconv.from_bytes(name);
        });
    } catch (json::exception e) {
        ShowError((strRes->Load(IDS_READ_CONFIG_FAILED) + wstrconv.from_bytes(e.what())).c_str());
    }
}

void WriteConfig() {
    auto devices = json::array();
    std::for_each(selectedDev.begin(), selectedDev.end(), [&devices](auto const& dev) {
        devices.push_back({ {CONFIG_ID, wstrconv.to_bytes(dev.first)}, {CONFIG_NAME, wstrconv.to_bytes(dev.second) }});
    });
    json config = { {CONFIG_DEFAULT_PROFILE_NAME, devices} };
    std::ofstream out(configFilePath);
    out << std::setw(2) << config;
    if (out.fail()) {
        ShowError(strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
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
    if (selectedDev.count(DEFAULT_MIC_DEV_ID)) {
        auto userData = std::pair<const wchar_t*, int>(devID, 0);
        host->GetDefaultDevID(IsDefaultDev, &userData);
        return userData.second == 0;
    }
    return selectedDev.find(devID) != selectedDev.end();
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

void EnumDevProc(const wchar_t* devID, void* userData) {
    auto data = (EnumDevProcData*)userData;
    if (data->ItemID >= lastMenuItemID) {
        SHOW_ERROR(L"Too many microphone devices. No more menu item id available!");
        return; // No more IDs available.
    }
    data->EnumedDevID.insert(devID);
    std::wstring name;
    host->GetDevIfaceName(devID, DevNameString, &name);
    UINT flags = MF_STRING;
    if (selectedDev.find(devID) != selectedDev.end()) {
        flags |= MF_CHECKED;
    }
    VERIFY(AppendMenu(devicesMenu, flags, data->ItemID, name.c_str()))
    menuID2Dev[data->ItemID] = std::pair<std::wstring, std::wstring>(devID, name);
    data->ItemID++;
}

void InitMenuListener() {
    if (devicesMenu) {
        DestroyMenu(devicesMenu);
        menuID2Dev.clear();
    }
    devicesMenu = CreatePopupMenu();

    UINT menuItemID = firstMenuItemID;
    allDevMenuID = menuItemID;
    VERIFY(AppendMenu(devicesMenu, 
        MF_STRING | (selectedDev.empty()? MF_CHECKED : 0), 
        allDevMenuID, strRes->Load(IDS_ALL_MICROPHONES).c_str()));
    menuItemID++;

    VERIFY(AppendMenu(devicesMenu, MF_SEPARATOR, 0, NULL));
    
    VERIFY(AppendMenu(devicesMenu, 
        MF_STRING | (selectedDev.size() == 1 && selectedDev.count(DEFAULT_MIC_DEV_ID) ? MF_CHECKED : 0),
        menuItemID, strRes->Load(IDS_DEFAULT_MICROPHONE).c_str()));
    menuID2Dev[menuItemID] = std::pair<std::wstring, std::wstring>(DEFAULT_MIC_DEV_ID, L"");
    menuItemID++;

    VERIFY(AppendMenu(devicesMenu, MF_SEPARATOR, 0, NULL));

    EnumDevProcData data = { menuItemID };
    host->GetActiveDevices(EnumDevProc, &data);

    // Append extra items in selectedDevID as menu items with
    // device ID as title.
    std::for_each(selectedDev.begin(), selectedDev.end(), [&data](const auto& dev) {
        if (dev.first == DEFAULT_MIC_DEV_ID || data.EnumedDevID.find(dev.first) != data.EnumedDevID.end()) {
            return;
        }
        VERIFY(AppendMenu(devicesMenu, MF_STRING|MF_CHECKED, data.ItemID, dev.second.c_str()));
        menuID2Dev[data.ItemID] = dev;
        data.ItemID++;
    });
    

    std::vector<wchar_t> rootTitle;
    if (selectedDev.empty()) {
        rootTitle = DupCStr(strRes->Load(IDS_APPLY_TO_ALL));
    }
    else if (selectedDev.size() == 1) {
        if (selectedDev.count(DEFAULT_MIC_DEV_ID)) {
            rootTitle = DupCStr(strRes->Load(IDS_APPLY_TO_DEFAULT));
        } else {
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
    VERIFY(host->ModifyRootMenuItem(state, &info));
}

static void OnMenuItemCmd(UINT id) {
    if (id == 0) { // The root menu item.
        return;
    }
    if (id == allDevMenuID) {
        selectedDev.clear();
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
    L"Profile",	    /*Name*/
    OnLoaded,		/*OnLoaded*/
    OnMenuItemCmd,	/*OnMenuItemCmd*/
};

extern "C" DeMic_PluginInfo * GetPluginInfo(void) {
    return &plugin;
}


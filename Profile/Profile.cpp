// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../sdk/DemicPlugin.h"
#include "../Util.h"
#include "resource.h"
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <strsafe.h>
#include <shlwapi.h>

static const wchar_t* const CONFIG_FILE_NAME = L"Profile.ini";
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

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;
    firstMenuItemID = args->FirstMenuItemID;
    lastMenuItemID = args->LastMenuItemID;

    host->SetInitMenuListener(state, InitMenuListener);
    host->SetDevFilter(MicDevFilter);

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

const static wchar_t CONFIG_DEVICES_DEL = L',';
const static std::wstring CONFIG_DEV_ID_NAME_DEL = L"|||";
const static wchar_t* const CONFIG_DEFAULT_PROFILE = L"Default";
const static wchar_t* const CONFIG_DEVICES = L"Devices";


void ReadConfig() {
    if (!PathFileExistsW(configFilePath.c_str())) {
        return;
    }
    wchar_t buf[2048] = { 0 };
    const auto bufSize = sizeof(buf) / sizeof(buf[0]);
    const auto gpps = GetPrivateProfileStringW(CONFIG_DEFAULT_PROFILE, CONFIG_DEVICES, L"", buf, bufSize, configFilePath.c_str());
    if (gpps == 0) {
        return;
    }
    if (gpps == bufSize - 1) {
        SHOW_LAST_ERROR();
        return;
    }
    std::wstringstream stream(buf);
    std::wstring devItem;
    while (!stream.eof()) {
        std::getline(stream, devItem, CONFIG_DEVICES_DEL);
        const auto sep = devItem.find_first_of(CONFIG_DEV_ID_NAME_DEL);
        selectedDev[devItem.substr(0, sep)] = devItem.substr(sep+CONFIG_DEV_ID_NAME_DEL.size());
    }
}

void WriteConfig() {
    std::wostringstream stream;
    if (!selectedDev.empty()) {
        auto it = selectedDev.begin();
        stream << it->first << CONFIG_DEV_ID_NAME_DEL << it->second;
        it++;
        for (; it != selectedDev.end(); ++it) {
            stream << CONFIG_DEVICES_DEL << it->first << CONFIG_DEV_ID_NAME_DEL << it->second;
        }
    }
    if (!WritePrivateProfileStringW(CONFIG_DEFAULT_PROFILE, CONFIG_DEVICES, stream.str().c_str(), configFilePath.c_str())) {
        SHOW_LAST_ERROR();
    }
}

BOOL MicDevFilter(const wchar_t* devID) {
    return selectedDev.empty() || // Empty: All devices.
        selectedDev.find(devID) != selectedDev.end();
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
    UINT flags = MF_STRING;
    if (selectedDev.empty()) {
        flags |= MF_CHECKED;
    }
    UINT menuItemID = firstMenuItemID;
    allDevMenuID = menuItemID;
    VERIFY(AppendMenu(devicesMenu, flags, allDevMenuID, strRes->Load(IDS_ALL_MICROPHONES).c_str()));
    menuItemID++;
    VERIFY(AppendMenu(devicesMenu, MF_SEPARATOR, 0, NULL));
    EnumDevProcData data = { menuItemID };
    host->GetActiveDevices(EnumDevProc, &data);

    // Append extra items in selectedDevID as menu items with
    // device ID as title.
    std::for_each(selectedDev.begin(), selectedDev.end(), [&data](const auto& dev) {
        if (data.EnumedDevID.find(dev.first) != data.EnumedDevID.end()) {
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
        rootTitle = DupCStr(strRes->Load(IDS_APPLY_TO_ONE));
    }
    else {
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
        auto dev = menuID2Dev[id];
        if (selectedDev.find(dev.first) != selectedDev.end()) {
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


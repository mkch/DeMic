#include "framework.h"
#include <sstream>
#include <algorithm>
#include "Plugin.h"
#include "DllPluginImpl.h"
#include "sdk/DeMicPlugin.h"
#include "DeMic.h"

const static wchar_t* const PLUGIN_EXT = L"plugin";

extern DeMic_Host host;

class DllPlugin : public Plugin {
public:
	DllPlugin (const std::wstring& displayName, HMODULE hDll, DeMic_PluginInfo* pluginInfo, UINT firstMenuItemID, UINT lastMenuItemID) :
		Plugin(displayName, firstMenuItemID, lastMenuItemID),
		hModule(hDll),
		PluginInfo(pluginInfo){}
public:
	const HMODULE hModule;
	const DeMic_PluginInfo* const PluginInfo;
	void (*MicMuteStateListener)() = NULL;
	void (*InitMenuListener)() = NULL;
	void (*DefaultDevChangedListener)() = NULL;
	BOOL(*DevFilter)(const wchar_t*) = NULL;
	std::unordered_map<HMENU, void(*)(HMENU)> InitMenuPopupListeners;
public:
	virtual bool OnLoaded() {
		DeMic_OnLoadedArgs args = { this, FirstMenuItemID + 1, LastMenuItemID };
		return PluginInfo->OnLoaded(&host, &args);
	}

	virtual bool HasDevFilter() {
		return DevFilter;
	}
	virtual BOOL CallDevFilter(const wchar_t* name) {
		return DevFilter(name);
	}

	virtual void CallMicMuteStateListener() {
		if (MicMuteStateListener) {
			MicMuteStateListener();
		}
	}

	virtual void CallInitMenuPopupListener(HMENU hMenu) {
		const auto it = InitMenuPopupListeners.find(hMenu);
		if (it != InitMenuPopupListeners.end()) {
			it->second(hMenu);
		}
	}

	virtual void CallDefaultDevChangedListener() {
		if (DefaultDevChangedListener) {
			DefaultDevChangedListener();
		}
	}

	virtual void CallOnMenuItemCmd(UINT id) {
		PluginInfo->OnMenuItemCmd(id);
	}

	virtual void Unload() {
		if (PluginInfo->OnUnload) {
			PluginInfo->OnUnload();
		}
		FreeLibrary(hModule);
	}
};


static BOOL HostDeleteRootMenuItem(void* st) {
	return DeletePluginRootMenuItem((DllPlugin*)st);
}
static void HostNotifyMicStateChanged() {
	NotifyMicStateChanged();
}

static void HostGetDefaultDevID(void(*callback)(const wchar_t* devID, void* userData), void* userData) {
	callback(micCtrl.GetDefaultMicphone().c_str(), userData);
}

static void HostSetDefaultDevChangedListener(void* st, void(*listener)()) {
	auto state = (DllPlugin*)st;
	state->DefaultDevChangedListener = listener;
}
static void HostGetActiveDevices(void (*callback)(const wchar_t* devID, void* userData), void* userData) {
	const auto devices = micCtrl.GetActiveDevices();
	std::for_each(devices.begin(), devices.end(), [callback, userData](const auto id) {
		callback(id.c_str(), userData);
		});
}

static void HostGetDevName(const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData) {
	callback(micCtrl.GetDevName(devID).c_str(), userData);
}

static void HostGetDevIfaceName(const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData) {
	callback(micCtrl.GetDevIfaceName(devID).c_str(), userData);
}

static BOOL HostGetDevMuted(const wchar_t* devID) {
	return micCtrl.GetDevMuted(devID);
}

static void HostSetDevFilter(void* st, BOOL(*filter)(const wchar_t* devID)) {
	((DllPlugin*)st)->DevFilter = filter;
}

static void HostSetInitMenuPopupListener(void* st, HMENU menu, void(*listener)(HMENU menu)) {
	auto state = (DllPlugin*)st;
	if (listener) {
		state->InitMenuPopupListeners[menu] = listener;
	}
	else {
		state->InitMenuPopupListeners.erase(menu);
	}
}

static void HostTurnOnMic(void* st) {
	TurnOnMic();
}

static void HostTurnOffMic(void* st) {
	TurnOffMic();
}

static void HostToggleMuted(void* st) {
	ToggleMuted();
}


static BOOL HostCreateRootMenuItem(void* st, LPCMENUITEMINFOW lpmi) {
	return CreateRootMenuItem((DllPlugin*)st, lpmi);
}

static BOOL HostModifyRootMenuItem(void* st, LPCMENUITEMINFOW lpmi) {
	return ModifyRootMenuItem((DllPlugin*)st, lpmi);
}

static BOOL HostIsMuted() {
	return IsMuted();
}

static void HostSetMicMuteStateListener(void* st, void(*listener)()) {
	DllPlugin* state = (DllPlugin*)st;
	state->MicMuteStateListener = listener;
}

static DeMic_Host host = {
	HostCreateRootMenuItem,
	HostTurnOnMic,
	HostTurnOffMic,
	HostToggleMuted,
	HostIsMuted,
	HostSetMicMuteStateListener,
	HostModifyRootMenuItem,
	HostGetActiveDevices,
	HostGetDevName,
	HostGetDevIfaceName,
	HostGetDevMuted,
	HostSetDevFilter,
	HostSetInitMenuPopupListener,
	HostNotifyMicStateChanged,
	HostGetDefaultDevID,
	HostSetDefaultDevChangedListener,
	HostDeleteRootMenuItem,
};

// Loads a plugin file. Returns true if the plugin is valid,
// false otherwise.
// If the plugin is valid, the HMODULE and the return value
// of GetPluginInfo is set to param info.
// It is the caller's responsibility to call FreeLibrary(info.first);
static bool LoadPluginInfo(const std::wstring& path, std::pair<HMODULE, DeMic_PluginInfo*>& info) {
	HMODULE hModule = LoadLibraryW(path.c_str());
	if (!hModule) {
		SHOW_LAST_ERROR();
		return false;
	}
	const DEMIC_GET_PLUGIN_INFO getPluginInfo = (DEMIC_GET_PLUGIN_INFO)GetProcAddress(hModule, DEMIC_GET_PLUGIN_INFO_FUNC_NAME);
	if (!getPluginInfo) {
		FreeLibrary(hModule);
		return false;
	}
	auto pluginInfo = getPluginInfo();
	if (!pluginInfo) {
		FreeLibrary(hModule);
		return false;
	}
	info.first = hModule;
	info.second = pluginInfo;
	return true;
}

static std::wstring GetPluginDisplayName(const std::wstring& path, const DeMic_PluginInfo* info) {
	return (std::wostringstream() << info->Name
		<< L" v" << info->Version.Major << L"." << info->Version.Minor
		<< L" (" << GetLastPathComponent(path) << L")").str();
}

// Reads all the plugins in plugin dir without loading them.
std::vector<PluginName>ReadPluginDir_DLL() {
	std::vector<PluginName> ret;
	EnumPluginDir(PLUGIN_EXT, [&ret](auto& path) {
		std::pair<HMODULE, DeMic_PluginInfo*> plugin;
		if (LoadPluginInfo(path, plugin)) {
			HMODULE hModule = plugin.first;
			DeMic_PluginInfo* info = plugin.second;
			ret.push_back(PluginName(path, GetPluginDisplayName(path, info)));
			FreeLibrary(hModule);
		}
		return true;
		});
	return ret;
}

std::unique_ptr<Plugin>LoadPlugin_DLL(const std::wstring& path, const std::pair<UINT, UINT>& menuItemIdRange) {
	std::pair<HMODULE, DeMic_PluginInfo*> info;
	if (!LoadPluginInfo(path, info)) {
		return NULL;
	}

	if (info.second->SDKVersion < 1) {
		FreeLibrary(info.first);
		ShowError((path + L"\nInvalid SDK Version!").c_str());
		return NULL;
	}
	if (info.second->SDKVersion > DEMIC_CURRENT_SDK_VERSION) {
		FreeLibrary(info.first);
		ShowError((path + L"\nRequires a higher SDK Version! Please update DeMic!").c_str());
		return NULL;
	}

	const auto firstID = menuItemIdRange.first;
	const auto lastID = menuItemIdRange.second;
	return std::make_unique<DllPlugin>(GetPluginDisplayName(path, info.second),  info.first, info.second, firstID, lastID);
}

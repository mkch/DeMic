#pragma once
 
#include <Windows.h>

extern "C" {
	struct DeMic_PluginInfo;

	// The export function name of plugin.
	static const char* DEMIC_GET_PLUGIN_INFO_FUNC_NAME = "GetPluginInfo";
	typedef DeMic_PluginInfo* (*DEMIC_GET_PLUGIN_INFO)(void);

	struct DeMic_Host;
	struct DeMic_OnLoadedArgs;

	static const DWORD DEMIC_CURRENT_SDK_VERSION = 1;
	
	struct DeMic_PluginInfo {
		// Version of this SDK.
		// Should be CURRENT_SDK_VERSION
		DWORD SDKVersion;
		// The name of this plugin. Can't be NULL.
		const wchar_t* Name;
		// Version of this plugin.
		struct { WORD Major; WORD Minor; } Version;
		// OnLoaded is called when this plugin is fulled loaded.
		// Argument host is the host application of DeMic.
		// Argument args is the extra arguments.
		// A plugin should return TRUE if everything is OK.
		BOOL (*OnLoaded)(DeMic_Host* host, DeMic_OnLoadedArgs* state);
		// OnMenuItemCmd is called when the root menu item or a sub menu item
		// of this plugin is selected.
		// Argument id is 0 if the root menu item is selected.
		// Can be NULL.
		void (*OnMenuItemCmd)(UINT id);
	};

	// Interface for plugin to call DeMic.
	struct DeMic_Host {
		// Creates the root menu item of this plugin.
		// MIIM_ID and wID of lpmi.fMask is ignored.
		BOOL(*CreateRootMenuItem)(void* state, LPCMENUITEMINFOW lpmi);
		// Turns on microphones.
		void(*TurnOnMic)(void* state);
		// Turns off microphones.
		void(*TurnOffMic)(void* state);
		// Toggles on off.
		void(*ToggleMuted)(void* state);
		// Gets the microphone state.
		BOOL(*IsMuted)();
		// Sets a listener to be called when micphone mute
		// state is changed.
		void(*SetMicMuteStateListener)(void* state, void(*listener)());
		// Modifies the root menu item of this plugin.
		// MIIM_ID and wID of lpmi.fMaks is ignored.
		BOOL(*ModifyRootMenuItem)(void* state, LPCMENUITEMINFOW lpmi);
		// Enumerates all activate microphone devices.
		void(*GetActiveDevices)(void (*callback)(const wchar_t* devID, void* userData), void* userData);
		// Gets the friendly name of the endpoint device
		// (for example, "Speakers (XYZ Audio Adapter)").
		void(*GetDevName)(const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData);
		// Gets the friendly name of the audio adapter to which the endpoint device is attached
		// (for example, "XYZ Audio Adapter")
		void(*GetDevIfaceName)(const wchar_t* devID, void(*callback)(const wchar_t* name, void* userData), void* userData);
		// Gets whether a microphone device is muted.
		BOOL(*GetDevMuted)(const wchar_t* devID);
		// Sets a filter function which defines the set of microphone devices
		// to operate.
		void(*SetDevFilter)(void* state, BOOL(*filter)(const wchar_t* devID));
		// Sets a listener called when a menu is about to popup.
		// NULL menu means the main system tray menu.
		// Setting a NULL listener removes the listening of the menu.
		void(*SetInitMenuPopupListener)(void* state, HMENU menu, void(*listener)(HMENU menu));
		// Forces DeMic to call micphone state changed handler.
		void(*NotifyMicStateChanged)();
		// Get the devault microphone device ID.
		// Empty string if not found.
		void(*GetDefaultDevID)(void(*callback)(const wchar_t* devID, void* userData), void* userData);
		// Sets a listener called when default microphone device is changed.
		void(*SetDefaultDevChangedListener)(void* state, void(*listener)());
		// Deletes the root menu item added by CreateRootMenuItem.
		BOOL(*DeleteRootMenuItem)(void* state);
	};

	// Extra arguments of OnLoaded in DeMic_PluginInfo.
	struct DeMic_OnLoadedArgs {
		// The opaque handle of internal plugin state.
		void* State;
		// Menu command ids within the range of [FirstMenuItemID,LastMenuItemID]
		// will be received by OnMenuItemCmd of DeMic_PluginInfo.
		UINT FirstMenuItemID;
		UINT LastMenuItemID;
	};

}

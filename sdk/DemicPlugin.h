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

	typedef void(*DeMic_MicStateListener)();

	// Interface for plugin to call DeMic.
	struct DeMic_Host {
		// Create the root menu item of this plugin.
		// MIIM_ID and wID of lpmi.fMask is ignored.
		BOOL(*CreateRootMenuItem)(void* state, LPCMENUITEMINFOW lpmi);
		// Turn on microphones.
		void (*TurnOnMic)(void* state);
		// Turn off microphones.
		void (*TurnOffMic)(void* state);
		// Toggle on off.
		void (*ToggleMuted)(void* state);
		// Get the microphone state.
		BOOL(*IsMuted)();
		// Set a listener to be called when micphone
		// state is changed.
		void (*SetMicStateListener)(void* state, DeMic_MicStateListener listener);
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

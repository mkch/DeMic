#pragma once

#include "MicCtrl.h"
#include "Util.h"

#define APSTUDIO_INVOKED
#include "resource.h"
const UINT APS_NextPluginCmdID = _APS_NEXT_COMMAND_VALUE;
#undef APSTUDIO_INVOKED

enum {
	// Notify message used by Shell_NotifyIconW.
	UM_NOTIFY = WM_USER + 1,
	// Microphone command message used by command line args.
	UM_MIC_CMD,
	// The window message sent to mainWindow when micophone muted state is changed.
	UM_MUTED_STATE_CHANGED,
	// The window message sent to mainWindow when audio device state is changed. 
	UM_DEVICE_STATE_CHANGED,
	// The window message sent to mainWindow when default microphone device is changed.
	UM_DEFAULT_DEVICE_CHANGED,

	// A plugin2 message is received.
	UM_RECV_PLUGIN2_MSG,
	// A plugin2 client disconnected.
	UM_PLUGIN2_DEAD,
};
// The main window of application.
extern HWND mainWindow;

extern HMENU popupMenu;
extern HMENU pluginMenu;
extern MicCtrl micCtrl;
extern StringRes* strRes;

void TurnOnMic();
void TurnOffMic();
void ToggleMuted();
BOOL IsMuted();
void WriteConfig();

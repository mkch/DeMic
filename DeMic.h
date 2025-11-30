#pragma once

#include "MicCtrl.h"
#include "Util.h"

#define APSTUDIO_INVOKED
#include "resource.h"
const UINT APS_NextPluginCmdID = _APS_NEXT_COMMAND_VALUE;
#undef APSTUDIO_INVOKED

// Currrent version of DeMic.
static const wchar_t* VERSION = L"1.2";

extern HMENU popupMenu;
extern HMENU pluginMenu;
extern MicCtrl micCtrl;
extern StringRes* strRes;
extern std::wstring defaultLogFilePath;

void TurnOnMic();
void TurnOffMic();
void ToggleMuted();
BOOL IsMuted();
void WriteConfig();

#pragma once

#include "resource.h"
#include "MicCtrl.h"

extern HMENU popupMenu;
extern MicCtrl micCtrl;

void TurnOnMic();
void TurnOffMic();
void ToggleMuted();
BOOL IsMuted();

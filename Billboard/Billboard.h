#pragma once
#include "../sdk/DemicPluginUtil.h"
#include "../Util.h"

extern HINSTANCE hInstance;
extern StringRes* strRes;
extern DeMic_Host* host;
extern void* state;
extern bool alwaysOnTop;
extern RECT lastWindowRect;

void ReadConfig();
void WriteConfig();

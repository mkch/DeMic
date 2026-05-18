#pragma once
#include "../sdk/DemicPluginUtil.h"
#include "../Util.h"

extern HINSTANCE hInstance;
extern std::unique_ptr<StringRes> strRes;
extern DeMic_Host* host;
extern void* state;
extern bool alwaysOnTop;
extern bool hideCaption;
extern RECT lastWindowRect;

void ReadConfig();
void WriteConfig();

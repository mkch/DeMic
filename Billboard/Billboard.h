#pragma once
#include "../sdk/DemicPluginUtil.h"
#include "../Util.h"

extern HINSTANCE hInstance;
extern std::unique_ptr<StringRes> strRes;
extern DeMic_Host* host;
extern void* state;
extern unsigned int configVersion;
extern bool alwaysOnTop;
extern bool hideCaption;

// Position and size of the Billboard window relative to the virtual screen, which is the
// bounding box of all displays. 0.0 is the left/top edge, 1.0 is the right/bottom edge.
struct WindowRectRatio {
	double left;
	double top;
	double right;
	double bottom;
};
extern WindowRectRatio lastWindowRect;

void ReadConfig();
void WriteConfig();

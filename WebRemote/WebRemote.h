#pragma once

#include "../sdk/DemicPluginUtil.h"

extern HINSTANCE hInstance;
extern DeMic_Host* host;
extern void* state;

// HOST_LOG logs message using host and state.
#define HOST_LOG(level, message) LOG(host, state, level, message)
#define HOST_LOG_WSTRING(level, message) LOG(host, state, level, message.c_str())
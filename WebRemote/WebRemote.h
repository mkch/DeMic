#pragma once

#include "../sdk/DemicPluginUtil.h"

extern HINSTANCE hInstance;
extern DeMic_Host* host;
extern void* state;

// Configuration in configuration file.
struct Configuration {
	std::string ServerListenHost;
	std::string ServerListenPort;
};

// Current configuration.
extern Configuration config;

// Starts the HTTP server with the specified listen host and port, and shows error message if failed.
bool StartHTTPServerWithPrompt(const std::string& listenHost, const std::string& listenPort);

void WriteConfig();

// HOST_LOG logs message using host and state.
#define HOST_LOG(level, message) LOG(host, state, level, message)
#define HOST_LOG_WSTRING(level, message) LOG(host, state, level, message.c_str())
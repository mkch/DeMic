#pragma once

#include "../Util.h"
#include "../sdk/DemicPluginUtil.h"

extern HINSTANCE hInstance;
extern DeMic_Host* host;
extern void* state;
extern StringRes* strRes;
extern std::filesystem::path moduleFilePath;	


struct HTTPSCert {
	std::string CertPemFilePath;
	std::string KeyPemFilePath;
};

// Configuration in configuration file.
struct Configuration {
	bool Enabled = false;
	std::string ServerListenHost;
	std::string ServerListenPort;
	bool EnableHTTPS = false;
	HTTPSCert HTTPSConfig;
};

// Current configuration.
extern Configuration config;

// Starts the HTTP server with the specified configuration, and shows error message if failed.
// Parameter parent is the parent window for the error message box. If it's NULL, host->GetMainWindow() will be used.
bool StartHTTPServerWithPrompt(const Configuration& config, HWND parent = NULL);

void WriteConfig();

// HOST_LOG logs message using host and state.
#define HOST_LOG(level, message) LOG(host, state, level, message)
#define HOST_LOG_WSTRING(level, message) LOG(host, state, level, message.c_str())
#pragma once
#include <string>
#include "WebRemote.h"

void InitHTTPServer();

enum HTTPServerResult {
	SERVER_OK,
	SERVER_EMPTY_CERT_OR_KEY_FILE,
	SERVER_INVALID_ADDRESS_FORMAT,
	SERVER_INVALID_PORT,
	SERVER_RESOLVE_ENDPOINT,
	SERVER_LISTEN_ERROR,
	SERVER_BIND_ERROR,
	SERVER_ERROR,
};

HTTPServerResult StartHTTPServer(const Configuration& config, std::wstring& errorMessage);
bool StopHTTPServer();
bool HTTPServerRunning();
std::string GetHTTPServerListenHost();
unsigned short GetHTTPServerListenPort();

void NotifyStateChange(bool newState);
void CancelStateChangeNotifications();
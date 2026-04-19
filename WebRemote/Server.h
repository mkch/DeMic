#pragma once
#include <string>

void InitHTTPServer();

enum HTTPServerResult {
	SERVER_OK,
	SERVER_INVALID_ADDRESS_FORMAT,
	SERVER_INVALID_PORT,
	SERVER_RESOLVE_ENDPOINT,
	SERVER_LISTEN_ERROR,
	SERVER_BIND_ERROR,
	SERVER_ERROR,
};

HTTPServerResult StartHTTPServer(const std::string& host, const std::string& port, std::wstring& errorMessage);
bool StopHTTPServer();
bool HTTPServerRunning();
std::string GetHTTPServerListenHost();
unsigned short GetHTTPServerListenPort();

void NotifyStateChange(bool newState);
void CancelStateChangeNotifications();
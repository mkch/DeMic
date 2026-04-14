#pragma once

void InitHTTPServer();

bool StartHTTPServer();
bool StopHTTPServer();

void NotifyStateChange(bool newState);
void CancelStateChangeNotifications();
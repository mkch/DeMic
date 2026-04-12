#pragma once

bool StartHTTPServer();
bool StopHTTPServer();

void NotifyStateChange(bool newState);
void CancelStateChangeNotifications();
#pragma once

#include "Semver.h"
#include <functional>

// CheckForUpdate starts a update check in a separate thread.
// When the check is done, it posts a doneMessage to the target window,
// then OnDoneMessage must be called with the message parameters to handle the result.
void CheckForUpdate(HWND target, UINT doneMessage);
// OnUpdateCheckDone handles the result of the update check, and must be called when the doneMessage is received.
void OnUpdateCheckDone(WPARAM wParam, LPARAM lParam, std::function<void(const WSemVer& current, const WSemVer& remote, const std::wstring& url)>, std::function<void()>onPending, std::function<void()>onError);
// CancelUpdateCheck cancels the update check if it's still running.
// It must be called when the target window is closing.
void CancelUpdateCheck();
// CheckingUpdate returns whether the update check is still running.
bool CheckingForUpdate();
#include "framework.h"
#include "AutoUpdateMessageBox.h"
#include "DeMic.h"
#include "Log.h"
#include "Util.h"
#include <string>

static HHOOK hook = nullptr;
static HWND target = nullptr;
static bool firstActivate = true;

static LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    switch (nCode) {
    case HCBT_CREATEWND: {
        if (target) {
            break;
        }
        HWND hWnd = reinterpret_cast<HWND>(wParam);
        WCHAR className[64];
        if (GetClassNameW(hWnd, className, _countof(className)) > 0 
            && wcscmp(className, L"#32770") == 0) {
            target = hWnd;
        }
        break;
    }
    case HCBT_ACTIVATE: {
        if (target != reinterpret_cast<HWND>(wParam)) {
            break;
        }
        if (!firstActivate) {
            break;
        }
        firstActivate = false;
        const HWND noButton = GetDlgItem(target, IDNO);
        const HWND cancelButton = GetDlgItem(target, IDCANCEL);
        if(!noButton || !cancelButton) {
            break;
		}
        RECT rcNoButton = {}, rcCancelButton = {};
        GetWindowRect(noButton, &rcNoButton);
		ScreenToClient(target, reinterpret_cast<LPPOINT>(&rcNoButton));
		ScreenToClient(target, reinterpret_cast<LPPOINT>(&rcNoButton) + 1);
        GetWindowRect(cancelButton, &rcCancelButton);
        ScreenToClient(target, reinterpret_cast<LPPOINT>(&rcCancelButton));
        ScreenToClient(target, reinterpret_cast<LPPOINT>(&rcCancelButton) + 1);
		wchar_t noText[64] = { 0 };
		GetWindowText(noButton, noText, _countof(noText));
        // Swap the 2 buttons' positions
        SetWindowPos(noButton, 0, rcCancelButton.left, rcCancelButton.top, rcCancelButton.right - rcCancelButton.left, rcCancelButton.bottom - rcCancelButton.top, SWP_NOZORDER);
        SetWindowPos(cancelButton, 0, rcNoButton.left, rcNoButton.top, rcNoButton.right - rcNoButton.left, rcNoButton.bottom - rcNoButton.top, SWP_NOZORDER);
		// Set text of No button to "Skip this version"
        auto skip = strRes->Load(IDS_SKIP_THIS_VERSION);
        SetWindowTextW(noButton, skip.c_str());
		// Set text of Cancel button to "No"
        SetWindowTextW(cancelButton, noText);
        break;
    }
    }
    return CallNextHookEx(hook, nCode, wParam, lParam);
}

int AutoUpdateMessageBox(const std::wstring& newVersion) {
	target = nullptr;
	firstActivate = true;
    hook = SetWindowsHookEx(
        WH_CBT,
        CBTProc,
        NULL,
        GetCurrentThreadId()
    );

    if (!hook) {
        LOG(Logger::LevelError, L"Failed to set windows hook");
    }
    auto message = strRes->Load(IDS_UPDATE_AVAILABLE) + newVersion + strRes->Load(IDS_UPDATE_OR_NOT);
    int result = ShowMessageBox(message.c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONQUESTION | MB_YESNOCANCEL);

    if (hook) {
        UnhookWindowsHookEx(hook);
    }

    hook = nullptr;
    firstActivate = true;
    target = nullptr;

    return result;
}


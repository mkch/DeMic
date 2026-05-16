#include "pch.h"
#include "MessageWindow.h"
#include "HotKeyPlus.h"
#include "../sdk/DemicPluginUtil.h"

static const wchar_t* wndClassName = L"Hot Key Plus Message Window";
static HWND messageWindow = NULL;

enum {
    UM_INITIAL_HOTKEY = WM_USER + 1, // Hotkey to initiate microphone operations.
};

enum {
	TIMER_PULL_ASYNC_KEY_STATE = 1, // Timer to check the state of hotkey.
};

static const UINT PULL_ASYNC_KEY_STATE_INTERVAL_MS = 100;

void DestroyMessageWindow() {
    if (!messageWindow) {
        throw std::logic_error("Message window not created");
    }
    if (messageWindow) {
        DestroyWindow(messageWindow);
        messageWindow = NULL;
        UnregisterClassW(wndClassName, hInstance);
    }
}

static void CALLBACK PullAsyncKeyState(HWND hwnd, UINT message, UINT_PTR idEvent, DWORD dwTime) {
    if (GetAsyncKeyState(VK_F12) >= 0 || GetAsyncKeyState(VK_CONTROL) >= 0) {
        KillTimer(messageWindow, idEvent);
        demicHost->TurnOffMic(demicState);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if(wParam != UM_INITIAL_HOTKEY) {
            break;
		}
        auto id = SetTimer(hwnd, TIMER_PULL_ASYNC_KEY_STATE, PULL_ASYNC_KEY_STATE_INTERVAL_MS, PullAsyncKeyState);
        if(id == 0) {
			LOG_LAST_ERROR(demicHost, demicState);
		}
        demicHost->TurnOnMic(demicState);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool CreateMessageWindow() {
    if (messageWindow) {
        throw std::logic_error("Message window already created");
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = wndClassName;

    if (!RegisterClassW(&wc)) {
        return false;
    }

    messageWindow = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        hInstance,
        nullptr
    );

    return messageWindow != NULL;
}

bool RegisterInitialHotKey(UINT vk, UINT modifiers) {
    if (!messageWindow) {
        throw std::logic_error("Message window not created");
    }
    return RegisterHotKey(messageWindow, UM_INITIAL_HOTKEY, modifiers|MOD_NOREPEAT, vk);
}

bool UnregisterInitialHotKey() {
    if (!messageWindow) {
        throw std::logic_error("Message window not created");
    }
    return UnregisterHotKey(messageWindow, UM_INITIAL_HOTKEY);
}
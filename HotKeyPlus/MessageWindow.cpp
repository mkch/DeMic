#include "pch.h"
#include "MessageWindow.h"
#include "HotKeyPlus.h"
#include "../sdk/DemicPluginUtil.h"
#include "resource.h"

static const wchar_t* wndClassName = L"Hot Key Plus Message Window";
static HWND messageWindow = NULL;

enum {
   HOTKEY_ID1 = 1,
   HOTKEY_ID2,
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
    HotKeyControlInfo info;
    info.SetValue(config.Hotkey);
    bool pressed = true;
    VK_INSERT;
    for(auto vk : info.GetVirtualKeys()) {
        if (GetAsyncKeyState(vk) >= 0) {
            pressed = false;
            break;
        }
	}
    if (!pressed) {
        KillTimer(messageWindow, idEvent);
        switch (config.Type) {
        case TYPE_PTT:
            demicHost->TurnOffMic(demicState);
            break;
        case TYPE_PTM:
            demicHost->TurnOnMic(demicState);
            break;
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID1) {
            if (config.Type == TYPE_TOGGLE) {
                demicHost->ToggleMuted(demicState);
                break;
            } else if (config.Type == TYPE_ON_OFF) {
                demicHost->TurnOnMic(demicState);
                break;
            }
            auto id = SetTimer(hwnd, TIMER_PULL_ASYNC_KEY_STATE, PULL_ASYNC_KEY_STATE_INTERVAL_MS, PullAsyncKeyState);
            if (id == 0) {
                LOG_LAST_ERROR(demicHost, demicState);
            }
            switch (config.Type) {
            case TYPE_PTT:
                demicHost->TurnOnMic(demicState);
                break;
            case TYPE_PTM:
                demicHost->TurnOffMic(demicState);
                break;
            }
        } else if(wParam == HOTKEY_ID2) {
            demicHost->TurnOffMic(demicState);
        } else {
			throw std::logic_error("Unknown hotkey ID");
        }
        
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

static bool RegisterHotKeyImpl (HWND parent, int id, const HotKeyControlInfo& info) {
    if (!messageWindow) {
        throw std::logic_error("Message window not created");
    }
    if (!info.RegisterHotKey(messageWindow, id, true)) {
        switch (GetLastError()) {
        case 0:
            break;
        case 1409: // 1409: ERROR_HOTKEY_ALREADY_REGISTERED
            ShowError(demicHost, demicState, strRes->Load(IDS_HOTKEY_CONFILCT).c_str(), parent);
            break;
        default:
            LOG_LAST_ERROR(demicHost, demicState);
            break;
        }
        return false;
    }
    return true;
}

bool RegisterHotKey1(HWND parent, const HotKeyControlInfo& info) {
	return RegisterHotKeyImpl(parent, HOTKEY_ID1, info);
}

bool RegisterHotKey2(HWND parent, const HotKeyControlInfo& info) {
    return RegisterHotKeyImpl(parent, HOTKEY_ID2, info);
}


void UnregisterHotKeys() {
    if (!messageWindow) {
        throw std::logic_error("Message window not created");
    }
    UnregisterHotKey(messageWindow, HOTKEY_ID1);
    UnregisterHotKey(messageWindow, HOTKEY_ID2);
}
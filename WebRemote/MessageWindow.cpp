#include "pch.h"
#include <windows.h>
#include "WebRemote.h"

static const wchar_t* wndClassName = L"Web Remote Message Window";
static HWND messageWindow = NULL;

enum {
    UM_TOGGLE = WM_USER + 1, // Toggle mic state.
};

bool PostToggle() {
    if (!messageWindow) {
        throw std::logic_error("Message window not created");
    }
	return PostMessageW(messageWindow, UM_TOGGLE, 0, 0);
}

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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case UM_TOGGLE:
        host->ToggleMuted(state);
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
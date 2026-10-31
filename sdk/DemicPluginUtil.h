#pragma once

#include "DemicPlugin.h"
#include <windows.h>
#include <sstream>
#include <format>

static inline int ShowMessageBox(DeMic_Host* host, void* state, const wchar_t* msg,
	HWND parent = NULL, UINT uType = 0) {
	// MB_TASKMODAL disables all window in this thread if the hwnd of MessageBoxW is null.
	// Note:
	// Whenever a modal dialog appears, the main window must be disabled.
	// DeMic relies on this behavior to prevent the main notification icon's context menu from popping up
	// while a modal dialog is displayed.
	return MessageBoxW(parent ? parent : host->GetMainWindow(state),
		msg,
		host->GetMessageCaption(state),
		(uType | MB_SETFOREGROUND | MB_TASKMODAL) & ~MB_SYSTEMMODAL);
}

static inline int ShowError(DeMic_Host* host, void* state, const wchar_t* msg, HWND parent = NULL) {
	return ShowMessageBox(host, state, msg, parent, MB_ICONERROR);
}

static inline void _LogError(DeMic_Host* host, void* state, const wchar_t* file, int line, DWORD lastError) {
	wchar_t* msg = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
	const std::wstring message = (std::wostringstream() << lastError << L": " << (msg ? msg : L"<unknown>")).str();
	host->WriteLog(state, LevelError, file, line, message.c_str());
	LocalFree(msg);
}

#define LOG(host, state, level, msg) (host)->WriteLog((state), (level), _CRT_WIDE(__FILE__), __LINE__, (msg))
#define LOG_ERROR(host, state, lastError) _LogError((host), (state), _CRT_WIDE(__FILE__), __LINE__, (lastError))
#define LOG_LAST_ERROR(host, state) LOG_ERROR(host, state, GetLastError())

// See ShowError for the reason of adding MB_TASKMODAL.
#define VERIFY_SIMPLE(title, exp) { \
	if (!(exp)) { \
		MessageBoxW(NULL, L"Verification failed: " _CRT_WIDE(#exp), (title), MB_ICONERROR | MB_TASKMODAL); \
		DebugBreak(); \
	} \
}

#define VERIFY(host, state, exp) { \
	if (!(exp)) { \
		(host)->WriteLog((state), LevelError, _CRT_WIDE(__FILE__), __LINE__, L"Verification failed: " _CRT_WIDE(#exp)); \
		DebugBreak(); \
	} \
}

#define VERIFY_SUCCEEDED(hr) { \
	if (FAILED(hr)) { \
		LogError(_CRT_WIDE(__FILE__), __LINE__, DWORD(hr)); \
		DebugBreak(); \
	} \
}

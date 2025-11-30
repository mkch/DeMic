#pragma once

#include "DemicPlugin.h"
#include <windows.h>
#include <sstream>

static inline void ShowError(const wchar_t* title, const wchar_t* msg) {
	MessageBoxW(NULL, msg, title, MB_ICONERROR);
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

#define VERIFY_SIMPLE(title, exp) { \
	if (!(exp)) { \
		MessageBoxW(NULL, L"Verification failed:\n" _CRT_WIDE(#exp), (title), MB_ICONERROR); \
		DebugBreak(); \
	} \
}

#define VERIFY(host, state, exp) { \
	if (!(exp)) { \
		(host)->WriteLog((state), LevelError, _CRT_WIDE(__FILE__), __LINE__, L"Verification failed:\n" _CRT_WIDE(#exp)); \
		DebugBreak(); \
	} \
}

#define VERIFY_OK(exp) VERIFY((exp) == S_OK)
#define VERIFY_SUCCEEDED(exp) VERIFY(SUCCEEDED(exp))

#pragma once

#include "sdk/DemicPlugin.h"

#define LOG(level, msg) WriteLog((level), _CRT_WIDE(__FILE__), __LINE__, (msg))
#define LOG_ERROR(err) LogError(_CRT_WIDE(__FILE__), __LINE__, err)
#define LOG_LAST_ERROR() LOG_ERROR(GetLastError())

#define VERIFY(exp) { \
	if (!(exp)) { \
		LogError(_CRT_WIDE(__FILE__), __LINE__, L"Verification failed:\n" _CRT_WIDE(#exp)); \
		DebugBreak(); \
	} \
}

#define VERIFY_SUCCEEDED(hr) { \
	if (FAILED(hr)) { \
		LogError(_CRT_WIDE(__FILE__), __LINE__, DWORD(hr)); \
		DebugBreak(); \
	} \
}

void WriteLog(LogLevel level, const wchar_t* file, int line, const std::wstring& message, const DeMic_PluginInfo* plugin = NULL);
std::wstring GetLastErrorMessage(DWORD lastError);
void ShowError(const wchar_t* msg);
void LogError(const wchar_t* file, int line, const wchar_t* msg);
void LogError(const wchar_t* file, int line, DWORD lastError);
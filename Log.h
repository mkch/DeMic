#pragma once

#include "sdk/DemicPlugin.h"
#include <string>
#include <ostream>

#define LOG(level, msg) Log((level), _CRT_WIDE(__FILE__), __LINE__, (msg))
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

// Display errors to the user prominently.
void ShowError(const wchar_t* msg);
// Set the default logger stream. The initial default logger is stderr.
void SetDefaultLogger(std::ostream*);
// Log filename, line number and message to the default logger.
void Log(LogLevel level, const wchar_t* file, int line, const wchar_t* msg, const DeMic_PluginInfo* plugin = NULL);
// Log filename, line number and error message to the default logger.
void LogError(const wchar_t* file, int line, const wchar_t* msg);
// Log filename, line number and the description of lastError to the default logger.
void LogError(const wchar_t* file, int line, DWORD lastError);
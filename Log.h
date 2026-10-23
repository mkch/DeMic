#pragma once

#include "sdk/DemicPlugin.h"
#include <string>
#include <ostream>
#include <fstream>
#include "Logger.h"

#define LOG(level, msg) Log((level), _CRT_WIDE(__FILE__), __LINE__, (msg))
#define LOG_ERROR(err) LogError(_CRT_WIDE(__FILE__), __LINE__, err)
#define LOG_LAST_ERROR() LOG_ERROR(GetLastError())

#define VERIFY(exp) { \
	if (!(exp)) { \
		LogError(_CRT_WIDE(__FILE__), __LINE__, L"Verification failed: " _CRT_WIDE(#exp)); \
		DebugBreak(); \
	} \
}

#define VERIFY_SUCCEEDED(hr) { \
	if (FAILED(hr)) { \
		LogError(_CRT_WIDE(__FILE__), __LINE__, DWORD(hr)); \
		DebugBreak(); \
	} \
}

// Set the default logger stream. The initial default logger is stderr.
void SetDefaultLogger(Logger* logger);
// Opens the log file with rotation. The caller owns the returned stream.
// The log file is filePath, and its backup files are named after the stem of filePath followed by
// -0, -1, ... and the extension (e.g. DeMic-0.log for DeMic.log), numbered consecutively from the
// oldest to the newest. If the log file already exceeds maxFileSize when this function is called,
// it is rotated to the newest backup file, and at most maxBackupFiles backup files are kept.
// The size of the backup files is not checked. Returns a stream in a failed state, i.e. not open
// and failbit set, if the log file could not be rotated or opened; otherwise the returned stream
// is opened in append mode.
std::ofstream OpenRotatingLogFile(const std::wstring& filePath,
	size_t maxFileSize = 1024 * 1024, size_t maxBackupFiles = 1);
// Display errors to the user prominently.
void ShowError(const wchar_t* msg);
// Log filename, line number and message to the default logger.
void Log(Logger::Level level, const wchar_t* file, int line, const wchar_t* msg, const DeMic_PluginInfo* plugin = NULL);
// Log filename, line number and error message to the default logger.
void LogError(const wchar_t* file, int line, const wchar_t* msg);
// Log filename, line number and the description of lastError to the default logger.
void LogError(const wchar_t* file, int line, DWORD lastError);
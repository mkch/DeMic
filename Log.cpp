#include <chrono>   // C++ time point
#include <ctime>    // C-style time
#include <iomanip>  // for std::put_time
#include <codecvt>  // for std::wstring_convert

#include <fstream>

#include "Log.h"
#include "DeMic.h"
#include "Util.h"

const wchar_t* file_path_base(const wchar_t* path, const wchar_t delim = L'\\');

std::tm GetLocalTm() {
	// Get current system time point
	auto now = std::chrono::system_clock::now();
	// Convert to C-style time_t
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);

	// Convert to local time (struct tm)
	std::tm tm;
	localtime_s(&tm, &now_c);
	return tm;
}

static std::wstring_convert<std::codecvt_utf8<wchar_t>> wstrconv;

static const char* LevelName(LogLevel level) {
	switch (level) {
	case LevelDebug:
		return "DEBUG";
	case LevelInfo:
		return "INFO ";
	case LevelWarn:
		return "WARN ";
	case LevelError:
		return "ERROR";
	default:
		return "?????";
	}
}

void WriteLog(LogLevel level, const wchar_t* file, int line, const std::wstring& message, const DeMic_PluginInfo* plugin) {
	const auto now = GetLocalTm();
	try {
		std::ofstream out(defaultLogFilePath, std::ios::app);
		out.exceptions(std::ios::failbit | std::ios::badbit);
		out << '[' << LevelName(level)  << "] "
			<< std::put_time(&now, "%Y-%m-%d_%H:%M:%S")
			<< " Demic-" << wstrconv.to_bytes(VERSION) << ' ';
		if (plugin) {
			out << wstrconv.to_bytes(plugin->Name) << '-' << plugin->Version.Major << '.' << plugin->Version.Minor << ' ';
		}
		out << wstrconv.to_bytes(file_path_base(file)) << ':' << line << ' '
			<< wstrconv.to_bytes(message)
			<< std::endl;
	} catch (...) {
		static bool errorShowed = false;
		if (!errorShowed) {
			ShowError( (std::wstring(L"Write log file field: ") + defaultLogFilePath).c_str());
			errorShowed = true;
		}
	}
}

// Extract the base part of a file path.
// E.g. file_path_base(L"C:\\a\\b\\c.txt",'\\') == "c.txt"
const wchar_t* file_path_base(const wchar_t* path, const wchar_t delim) {
	if (delim == 0) return path;
	const size_t len = std::wcslen(path);
	if (len == 0) return path;
	auto p = path + len - 1;
	do {
		if (*p == delim) {
			return p + 1;
		}
		p--;
	} while (p >= path);
	return path;
}

void LogError(const wchar_t* file, int line, const wchar_t* msg) {
	WriteLog(LevelError, file, line, msg, NULL);
}

static std::wstring GetLastErrorMessage(DWORD lastError) {
	wchar_t* msg = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
	const std::wstring message = (std::wostringstream() << lastError << L": " << (msg ? msg : L"<unknown>")).str();
	LocalFree(msg);
	return message;
}

// Log the error description of lastError.
void LogError(const wchar_t* file, int line, DWORD lastError) {
	LogError(file, line, GetLastErrorMessage(lastError).c_str());
}

void ShowError(const wchar_t* msg) {
	MessageBoxW(NULL, msg, appTitle.c_str(), MB_ICONERROR);
}
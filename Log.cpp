#include "Log.h"
#include "Logger.h"
#include "DeMic.h"

static Logger defaultLoggerImpl(&std::cerr, Logger::LevelDebug);
static Logger* defaultLogger = &defaultLoggerImpl;

void SetDefaultLogger(Logger* logger) {
	defaultLogger = logger;
}

void Log(Logger::Level level, const wchar_t* file, int line, const wchar_t* msg, const DeMic_PluginInfo* plugin) {
	std::wstringstream ss;
	ss << L"DeMic-" << VERSION << L" ";
	if(plugin) {
		ss << plugin->Name << L'-' << plugin->Version.Major << L"." << plugin->Version.Minor << L" ";
	}
	ss << msg;

	if (!defaultLogger->Log(level, file, line, ss.str())) {
		static bool errorShowed = false;
		if (!errorShowed) {
			ShowError(L"Write log failed!");
			errorShowed = true;
		}
	}
}

void LogError(const wchar_t* file, int line, const wchar_t* msg) {
	Log(Logger::LevelError, file, line, msg);
}

static std::wstring GetLastErrorMessage(DWORD lastError) {
	wchar_t* msg = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
	const std::wstring message = (std::wostringstream() << lastError << L": " << (msg ? msg : L"<unknown>")).str();
	LocalFree(msg);
	return message;
}

void LogError(const wchar_t* file, int line, DWORD lastError) {
	LogError(file, line, GetLastErrorMessage(lastError).c_str());
}

void ShowError(const wchar_t* msg) {
	MessageBoxW(NULL, msg, appTitle.c_str(), MB_ICONERROR);
}
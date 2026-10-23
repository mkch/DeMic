#include "Log.h"
#include "Logger.h"
#include "DeMic.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

static Logger defaultLoggerImpl(&std::cerr, Logger::LevelDebug);
static Logger* defaultLogger = &defaultLoggerImpl;

void SetDefaultLogger(Logger* logger) {
	defaultLogger = logger;
}

static fs::path RotationFilePath(const fs::path& logFilePath, size_t n) {
	return logFilePath.parent_path() / std::format(L"{}-{}{}", logFilePath.stem().wstring(), n, logFilePath.extension().wstring());
}

// A stream in the same state as one whose open() failed: not open and failbit set.
static std::ofstream FailedLogFile() {
	std::ofstream stream;
	stream.setstate(std::ios::failbit);
	return stream;
}

std::ofstream OpenRotatingLogFile(const std::wstring& filePath, size_t maxFileSize, size_t maxBackupFiles) {
	const fs::path logFilePath(filePath);
	std::ios_base::openmode mode = std::ios::app;

	// Rotating.
	if (maxBackupFiles > 0 && fs::exists(logFilePath)) {
		std::error_code ec;
		const auto fileSize = fs::file_size(logFilePath, ec);
		if (ec) {
			return FailedLogFile();
		}
		if (fileSize >= maxFileSize) {
			// The backups are numbered from the oldest (0) to the newest, without gaps.
			std::vector<size_t> backups;
			for (size_t i = 0; i < maxBackupFiles; i++) {
				if (fs::exists(RotationFilePath(logFilePath, i))) {
					backups.push_back(i);
				}
			}
			// All the backup slots are in use:
			// drop the oldest one to make room for the current log file's backup.
			if (backups.size() == maxBackupFiles) {
				backups.erase(backups.begin());
			}
			// Close the gaps, oldest first. This never overwrites a backup that has not been moved yet.
			for (size_t i = 0; i < backups.size(); i++) {
				if (backups[i] != i) {
					fs::rename(RotationFilePath(logFilePath, backups[i]), RotationFilePath(logFilePath, i), ec);
					if (ec) {
						return FailedLogFile();
					}
				}
			}
			// Rotate the log file to the newest backup file.
			fs::rename(logFilePath, RotationFilePath(logFilePath, backups.size()), ec);
			if (ec) {
				return FailedLogFile();
			}
			mode = std::ios::trunc; // Create a new log file.
		}
	}

	return std::ofstream(logFilePath, mode);
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
	MessageBoxW(mainWindow, msg, appTitle.c_str(), MB_ICONERROR);
}
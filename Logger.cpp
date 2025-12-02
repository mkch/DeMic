#include "Logger.h"
#include <iomanip>
#include <ctime>

std::wstring_convert<std::codecvt_utf8<wchar_t>> Logger::sWstrConv;

// Extract the base part of a file path.
// E.g. file_path_base(L"C:\\a\\b\\c.txt",'\\') == "c.txt"
static const wchar_t* file_path_base(const wchar_t* path, const wchar_t delim = L'\\') {
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

bool Logger::Log(Level level, const wchar_t* file, int line, const std::wstring& message) {
    if (level >= mLevel) {
        return true;
    }

    // Current time
    std::time_t t = std::time(nullptr);
    std::tm tm = { 0 };
    localtime_s(&tm, &t);

    // Level string
    const char* levelStr = "????";
    switch (level) {
    case LevelDebug: levelStr = "DEBUG"; break;
    case LevelInfo: levelStr = "INFO"; break;
    case LevelWarn: levelStr = "WARN"; break;
    case LevelError: levelStr = "ERROR"; break;
    }

	// Output log message
    (*mStream) << "[" << std::put_time(&tm, "%Y-%m-%d_%H:%M:%S") << "] "
               << levelStr << " "
               << sWstrConv.to_bytes(file_path_base(file)) << ":" << line << " "
               << sWstrConv.to_bytes(message) << std::endl;
    const bool good = mStream->good();
    if (!good) {
		mStream->clear();
    }
    return good;
}
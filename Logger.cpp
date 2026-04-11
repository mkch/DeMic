#include "Logger.h"
#include <iomanip>
#include <ctime>
#include "Util.h"

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
    if (level < mLevel) {
        return true;
    }

    // Current time
    std::time_t t = std::time(nullptr);
    std::tm tm = { 0 };
    localtime_s(&tm, &t);

    // Level string
    std::string levelStr { 
        level == LevelDebug ? "DEBUG" :
        level == LevelInfo ? "INFO" : 
        level == LevelWarn ? "WARN" : 
        level == LevelError ? "ERROR" : 
        std::to_string(level)
    };

	// Output log message
    (*mStream) << "[" << std::put_time(&tm, "%Y-%m-%d_%H:%M:%S") << "] "
               << levelStr << " "
               << (const char*)ToUTF8(file_path_base(file)).c_str() << ":" << line << " "
               << (const char*)ToUTF8(message).c_str() << std::endl;
    const bool good = mStream->good();
    if (!good) {
		mStream->clear();
    }
    return good;
}
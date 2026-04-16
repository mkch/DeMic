#pragma once
#include <iostream>
#include <codecvt>
#include <mutex>

class Logger {
public:
	enum Level {
		LevelDebug = -4,
		LevelInfo = 0,
		LevelWarn = 4,
		LevelError = 8,
	};
private:
	std::mutex mMutex;
	std::ostream* mStream;
	Level mLevel;
public:
	Logger(std::ostream* stream, Level level) :
		mStream(stream), mLevel(level) {}
	Logger(const Logger&) = delete;

	bool Log(Level level, const wchar_t* file, int line, const std::wstring& message);
};


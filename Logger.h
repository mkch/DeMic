#pragma once
#include <iostream>
#include <codecvt>

class Logger {
public:
	enum Level {
		LevelDebug = -4,
		LevelInfo = 0,
		LevelWarn = 4,
		LevelError = 8,
	};
private:
	std::ostream* mStream;
	Level mLevel;
	static std::wstring_convert<std::codecvt_utf8<wchar_t>> sWstrConv;
public:
	Logger(std::ostream* stream, Level level) :
		mStream(stream), mLevel(level) {}
	Logger(const Logger&) = delete;

	bool Log(Level level, const wchar_t* file, int line, const std::wstring& message);
};


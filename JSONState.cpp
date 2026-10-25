#include "JSONState.h"
#include <fstream>

JSONState::JSONState(const std::wstring& filePath) : filePath(filePath) {
	std::ifstream in(filePath);
	if (!in) {
		LOG(Logger::LevelDebug, std::format(L"Failed to open JSON file: {}", filePath).c_str());
		return; // Empty state if file does not exist or cannot be opened.
	}
	try {
		in >> state;
	} catch (...) {
		LOG_ERROR(std::format(L"Failed to parse JSON from file: {}", filePath).c_str());
		return; // Empty state if file is not valid JSON.
	}
}

void JSONState::Save() {
	std::ofstream out(filePath);
	if(!out) {
		LOG_ERROR(std::format(L"Failed to open JSON file for writing: {}", filePath).c_str());
		return;
	}
	try {
		out << state.dump(4); // Pretty print with 4 spaces indentation.
	} catch (...) {
		LOG_ERROR(std::format(L"Failed to write JSON to file: {}", filePath).c_str());
	}
}
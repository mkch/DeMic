#pragma once

#include "nlohmann/json.hpp"
#include "TimeDebouncer.h"
#include "Log.h"


using json = nlohmann::json;

class JSONState {
private:
	const std::wstring filePath;
	json state;
	TimeDebouncer<> commitDebouncer{ 1000, [this]() { Save(); }, []() {LOG_LAST_ERROR(); } };
public:
	JSONState(const std::wstring& filePath);
	~JSONState() {
		commitDebouncer.Cancel();
		Save();
	}
private:
	void Save();
public:
	json& GetState() { return state; };
	void Commit() { commitDebouncer.Emit(); }
};


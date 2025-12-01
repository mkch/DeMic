#include "TimeDebouncer.h"

std::map<UINT_PTR, TimeDebouncer*> TimeDebouncer::sInstances;

void CALLBACK TimeDebouncer::TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
	if (!KillTimer(NULL, id)) {
		LOG_LAST_ERROR();
	}
	auto instance = sInstances[id];
	sInstances.erase(id);
	instance->mTimerID = 0;
	instance->onComplete();
}

TimeDebouncer::~TimeDebouncer() {
	if (mTimerID) {
		if (!KillTimer(NULL, mTimerID)) {
			LOG_LAST_ERROR();
		}
		sInstances.erase(mTimerID);
	}
}

void TimeDebouncer::Emit() {
	if (mTimerID) {
		if (!KillTimer(NULL, mTimerID)) {
			LOG_LAST_ERROR();
		}
	}
	mTimerID = SetTimer(NULL, 0, mInterval, TimerProc);
	if (!mTimerID) {
		LOG_LAST_ERROR();
		return;
	}
	sInstances[mTimerID] = this;
}
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
	// SetTimer will replace the timer if time id is not zero,
	// so we don't need to kill the previous timer.
	mTimerID = SetTimer(NULL, mTimerID, mInterval, TimerProc);
	if (!mTimerID) {
		LOG_LAST_ERROR();
		return;
	}
	sInstances[mTimerID] = this;
}
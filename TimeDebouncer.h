#pragma once

#include <windows.h>
#include <functional>
#include <map>
#include "Log.h"

template<class... Args>
class TimeDebouncer {
private:
	const UINT mInterval = 0;
	UINT_PTR mTimerID = 0;
	std::function<void(Args...)> mOnComplete;
	std::function<void()> mOnCompleteClosure; // mOnComplete and Args... are captured here.
private:
	static std::map<UINT_PTR, TimeDebouncer*> sInstances; // Map of timer IDs to TimeDebouncer instances.
	static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
		if (!KillTimer(NULL, id)) {
			LOG_LAST_ERROR();
		}
		auto instance = sInstances[id];
		sInstances.erase(id);
		instance->mTimerID = 0;
		instance->mOnCompleteClosure();
	}
public:
	// Construct a TimeDebouncer with the specified interval and onComplete function.
	// The onComplete function is called when the interval elapses without Emit being called again.
	TimeDebouncer(UINT interval, const std::function<void(Args...)>& onComplete)
		: mInterval(interval), mOnComplete(onComplete) {}

	void Emit(Args... args) {
		// SetTimer will replace the timer if time id is not zero,
		// so we don't need to kill the previous timer.
		mTimerID = SetTimer(NULL, mTimerID, mInterval, TimerProc);
		if (!mTimerID) {
			LOG_LAST_ERROR();
			return;
		}
		mOnCompleteClosure = [this, args...]() {
			return mOnComplete(args...);
			};
		sInstances[mTimerID] = this;
	}
};


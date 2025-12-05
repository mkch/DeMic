#pragma once

#include <windows.h>
#include <functional>
#include <map>

template<class... Args>
class TimeDebouncer {
private:
	const UINT mInterval = 0;
	UINT_PTR mTimerID = 0;
	const std::function<void()> mLogLastError;
	const std::function<void(Args...)> mOnComplete;
	std::function<void()> mOnCompleteClosure; // mOnComplete and Args... are captured here.
private:
	static std::map<UINT_PTR, TimeDebouncer*> sInstances; // Map of timer IDs to TimeDebouncer instances.
	static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
		auto instance = sInstances[id];
		if (!KillTimer(NULL, id)) {
			instance->mLogLastError();
		}
		sInstances.erase(id);
		instance->mTimerID = 0;
		instance->mOnCompleteClosure();
	}
public:
	// Construct a TimeDebouncer with the specified interval and onComplete function.
	// The onComplete function is called when the interval elapses without Emit being called again.
	// The logLastErrorFunc will be called to log the error message of GetLastError() when SetTimer or KillTimer fails.
	TimeDebouncer(UINT interval, const std::function<void(Args...)>& onComplete, const std::function<void()>& logLastErrorFunc)
		: mInterval(interval), mOnComplete(onComplete), mLogLastError(logLastErrorFunc){}

	~TimeDebouncer() {
		Cancel();
	}

	void Emit(Args... args) {
		// SetTimer will replace the timer if time id is not zero,
		// so we don't need to kill the previous timer.
		mTimerID = SetTimer(NULL, mTimerID, mInterval, TimerProc);
		if (!mTimerID) {
			mLogLastError();
			return;
		}
		mOnCompleteClosure = [this, args...]() {
			return mOnComplete(args...);
			};
		sInstances[mTimerID] = this;
	}

	void Cancel() {
		if (mTimerID) {
			if (!KillTimer(NULL, mTimerID)) {
				mLogLastError();
			}
			sInstances.erase(mTimerID);
			mTimerID = 0;
		}
	}
};


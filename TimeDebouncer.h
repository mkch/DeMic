#pragma once

#include <windows.h>
#include <functional>
#include <stdexcept>

// TimeDebouncer is a single-thread time debouncer. NOT THREAD-SAFE!
template<class... Args>
class TimeDebouncer{
private:
	bool mTimerScheduled = false;
	const UINT mInterval = 0;
	const std::function<void()> mLogLastError;
	const std::function<void(Args...)> mOnComplete;
	std::function<void()> mOnCompleteClosure; // mOnComplete and Args... are captured here.
	HWND mMsgWnd = nullptr;
private:
	static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
		auto instance = Instance(id);
		// Copy mOnCompleteClosure to avoid use-after-free.
		// If Emit() is called in mOnComplete, mOnCompleteClosure will be overwritten.
		auto onComplateClosureCopy = instance->mOnCompleteClosure;
		if (!KillTimer(hwnd, id)) {
			instance->mLogLastError();
		}
		// Clear the flag before invoking the closure,
		// which may schedule a new timer by calling Emit().
		instance->mTimerScheduled = false;
		onComplateClosureCopy();
	}
private:
	UINT_PTR TimerID() const {
		return reinterpret_cast<UINT_PTR>(this);
	}
	static TimeDebouncer* Instance(UINT_PTR timerID) {
		return reinterpret_cast<TimeDebouncer*>(timerID);
	}
public:
	// Construct a TimeDebouncer with the specified interval and onComplete function.
	// The onComplete function is called when the interval elapses without Emit being called again.
	// The logLastErrorFunc will be called to log the error message of GetLastError() when SetTimer or KillTimer fails.
	TimeDebouncer(UINT interval, const std::function<void(Args...)>& onComplete, const std::function<void()>& logLastErrorFunc)
		: mInterval(interval), mOnComplete(onComplete), mLogLastError(logLastErrorFunc){}

	// Not copyable. A copy would share mMsgWnd while the timer is bound to the
	// original instance, so both destructors would destroy the same window.
	// Copy assignment is implicitly deleted because of the const members.
	TimeDebouncer(const TimeDebouncer&) = delete;

	~TimeDebouncer() {
		if (mMsgWnd) {
			// Destroying window destroies any associated timers.
			DestroyWindow(mMsgWnd);
		}
	}

	void Emit(Args... args) {
		if (!mMsgWnd) {
			mMsgWnd = CreateWindowExW(
				0,
				L"Button",
				L"",
				0,
				0, 0, 0, 0,
				HWND_MESSAGE,
				nullptr,
				GetModuleHandleW(nullptr),
				nullptr
			);

			if (!mMsgWnd) {
				mLogLastError();
				return;
			}
		}
		// SetTimer will replace the timer if time id is not zero,
		// so we don't need to kill the previous timer.
		if (!SetTimer(mMsgWnd, TimerID(), mInterval, TimerProc)) {
			mLogLastError();
			return;
		}
		// Overwrites mOnCompleteClosure.
		// TimerProc should keep a copy of it before calling.
		mOnCompleteClosure = [this, args...]() { return mOnComplete(args...); };
		mTimerScheduled = true;
	}

	void Cancel() {
		if (!mTimerScheduled) {
			return;
		}
		if (!KillTimer(mMsgWnd, TimerID())) {
			mLogLastError();
		}
		mTimerScheduled = false;
	}
};


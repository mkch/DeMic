#pragma once

#include <windows.h>
#include <functional>
#include <stdexcept>
#include <cassert>

namespace detail {
	// Message-only window shared by all TimeDebouncer instances.
	class TimeDebouncerMsgWnd {
	protected:
		// Returns the shared window, creating it if necessary.
		// Returns nullptr if the window cannot be created.
		static HWND SharedMsgWnd();
		static void AddInstance();
		// Destroys the shared window when the last instance is released.
		// Returns false if destroying the window failed.
		static bool RemoveInstance();
		static HWND sHwnd; // The shared window, nullptr if not created yet.
	private:
		static size_t sInstanceCount;
	};
}

// TimeDebouncer is a single-thread time debouncer. NOT THREAD-SAFE!
// All instances share one message-only window, so they must be created, used
// and destroyed on the same thread. The shared window lives as long as at least
// one instance exists, so its lifetime does not depend on the debouncing state.
template<class... Args>
class TimeDebouncer: private detail::TimeDebouncerMsgWnd{
private:
	bool mTimerInstalled = false;
	const UINT mInterval = 0;
	const std::function<void()> mLogLastError;
	const std::function<void(Args...)> mOnComplete;
	std::function<void()> mOnCompleteClosure; // mOnComplete and Args... are captured here.
private:
	static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
		auto instance = Instance(id);
		if (!instance->mTimerInstalled) {
			// A WM_TIMER of an already canceled timer.
			return;
		}
		// Copy mOnCompleteClosure to avoid use-after-free.
		// If Emit() is called in mOnComplete, mOnCompleteClosure will be overwritten.
		auto onComplateClosureCopy = instance->mOnCompleteClosure;
		// The timer is done once WM_TIMER arrives. Clear the flag before invoking
		// the closure, which may install a new timer by calling Emit().
		instance->mTimerInstalled = false;
		if (!KillTimer(hwnd, id)) {
			instance->mLogLastError();
		}
		onComplateClosureCopy();
	}
private:
	UINT_PTR TimerID() const {
		return reinterpret_cast<UINT_PTR>(this);
	}
	static TimeDebouncer* Instance(UINT_PTR timerID) {
		return reinterpret_cast<TimeDebouncer*>(timerID);
	}
	void InstallTimer(Args... args) {
		// Overwrites mOnCompleteClosure.
		// TimerProc should keep a copy of it before calling.
		mOnCompleteClosure = [this, args...]() { return mOnComplete(args...); };
		// SetTimer replaces the timer if the timer ID is already in use,
		// so we don't need to kill the previous timer.
		assert(sHwnd); // TimeDebouncer must use a shared message window.
		if (!SetTimer(sHwnd, TimerID(), mInterval, TimerProc)) {
			mLogLastError();
			return;
		}
		mTimerInstalled = true;
	}
public:
	// Construct a TimeDebouncer with the specified interval and onComplete function.
	// The onComplete function is called when the interval elapses without Emit being called again.
	// The logLastErrorFunc will be called to log the error message of GetLastError() when SetTimer or KillTimer fails.
	TimeDebouncer(UINT interval, const std::function<void(Args...)>& onComplete, const std::function<void()>& logLastErrorFunc)
		: mInterval(interval), mOnComplete(onComplete), mLogLastError(logLastErrorFunc) {
		AddInstance();
	}

	// Not copyable.
	TimeDebouncer(const TimeDebouncer&) = delete;

	~TimeDebouncer() {
		Cancel();
		if (!RemoveInstance()) {
			mLogLastError();
		}
	}

	void Emit(Args... args) {
		if (!SharedMsgWnd()) {
			mLogLastError();
			return;
		}
		InstallTimer(args...);
	}

	void Cancel() {
		if (!mTimerInstalled) {
			return;
		}
		mTimerInstalled = false;
		if (!KillTimer(sHwnd, TimerID())) {
			mLogLastError();
		}
	}
};



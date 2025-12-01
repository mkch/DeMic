#pragma once

#include <windows.h>
#include <functional>
#include <map>
#include "Log.h"

class TimeDebouncer {
private:
	const UINT mInterval = 0;
	UINT_PTR mTimerID = 0;
	std::function<void()> onComplete;
private:
	static std::map<UINT_PTR, TimeDebouncer*> sInstances; // Map of timer IDs to TimeDebouncer instances.
	static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);
public:
	// Construct a TimeDebouncer with the specified interval and onComplete function.
	// The onComplete function is called when the interval elapses without Emit being called again.
	TimeDebouncer(UINT interval, std::function<void()> onComplete) 
		: mInterval(interval),onComplete(onComplete){}
	~TimeDebouncer();
	TimeDebouncer(const TimeDebouncer&) = delete;
public:
	// Emits an event. If no other Emit is called within the interval, the onComplete function is called.
	void Emit();
};


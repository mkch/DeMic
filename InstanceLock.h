#pragma once
#include "framework.h"
#include "Log.h"

class InstanceLock {
private:
	HANDLE hMutex = nullptr;
public:
	InstanceLock() {
		hMutex = CreateMutexW(NULL, FALSE, L"DeMic Instance Mutex");
		if (hMutex == NULL) {
			LOG_LAST_ERROR();
			std::exit(1);
		}
	}

	InstanceLock(const InstanceLock&) = delete;

	~InstanceLock() {
		if (hMutex) {
			CloseHandle(hMutex);
		}
	}
public:
	// Acquire the lock. Returns true if the lock is acquired, false if timeout.
	bool Acquire(DWORD timeout) {
		const DWORD w = WaitForSingleObject(hMutex, timeout);
		if (w == WAIT_FAILED) {
			LOG_LAST_ERROR();
			return false;
		}
		return (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED);
	}
	// TryAcquire try to acquire the lock. Returns true if the lock is acquired, false if another instance is running.
	bool TryAcquire() {
		return Acquire(0);
	}
};
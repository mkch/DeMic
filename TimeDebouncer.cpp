#include "TimeDebouncer.h"

namespace detail {
	HWND TimeDebouncerMsgWnd::sHwnd = nullptr;
	size_t TimeDebouncerMsgWnd::sInstanceCount = 0;

	HWND TimeDebouncerMsgWnd::SharedMsgWnd() {
		if (sHwnd) {
			return sHwnd;
		}
		sHwnd = CreateWindowExW(
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
		return sHwnd;
	}

	void TimeDebouncerMsgWnd::AddInstance() {
		++sInstanceCount;
	}

	bool TimeDebouncerMsgWnd::RemoveInstance() {
		if (sInstanceCount == 0) {
			return true; // Unbalanced, nothing to release.
		}
		if (--sInstanceCount > 0) {
			return true;
		}
		if (!sHwnd) {
			return true;
		}
		if (!DestroyWindow(sHwnd)) {
			return false;
		}
		sHwnd = nullptr;
		return true;
	}
}

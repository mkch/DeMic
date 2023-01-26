#pragma once
#include <windows.h>
#include <string>
#include <map>
#include <vector>

extern std::wstring utilAppName;

#define SHOW_ERROR(err) ShowError(err, _CRT_WIDE(__FILE__), __LINE__)
#define SHOW_LAST_ERROR() SHOW_ERROR(GetLastError())

#define VERIFY(exp) { \
	if (!(exp)) { \
		ShowError(L"Verification failed:\n" _CRT_WIDE(#exp), _CRT_WIDE(__FILE__), __LINE__); \
		DebugBreak(); \
	} \
}

#define VERIFY_OK(exp) VERIFY((exp) == S_OK)
#define VERIFY_SUCCEEDED(expr) VERIFY(SUCCEEDED(expr))

void ShowError(const wchar_t* msg);
void ShowError(const wchar_t* msg, const wchar_t* file, int line);
void ShowError(DWORD lastError, const wchar_t* file, int line);

class StringRes {
public:
	explicit StringRes(HMODULE hModule) :hInstance(hModule) {}
private:
	StringRes(const StringRes&);
private:
	const HMODULE hInstance;
	std::map<UINT, std::wstring> stringResMap;
public:
	const std::wstring& Load(UINT resId);
};

// DupCStr returns a copy of c_str().
std::vector<wchar_t> DupCStr(const std::wstring& str);
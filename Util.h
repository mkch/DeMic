#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
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
	std::unordered_map<UINT, std::wstring> stringResMap;
public:
	const std::wstring& Load(UINT resId);
};

// DupCStr returns a copy of c_str().
std::vector<wchar_t> DupCStr(const std::wstring& str);

// Join joins elements in a range with a delimiter.
template<typename Iterator, typename Delimiter>
std::wstring Join(const Iterator& begin, const Iterator& end, const Delimiter& del) {
	auto it = begin;
	if (it == end) {
		return L"";
	}
	std::wostringstream stream;
	stream << *it;
	for (++it; it != end; it++) {
		stream << del << *it;
	}
	return stream.str();
}

// Split splits a string and call f(component) for each components.
template<typename F>
void Split(const std::wstring& str, const std::wstring& del, const F& f) {
	if (str.empty()) {
		return;
	}
	size_t off = 0;
	while (true) {
		auto pos = str.find(del, off);
		if (pos == std::wstring::npos) {
			f(str.substr(off));
			break;
		}
		f(str.substr(off, pos - off));
		off = pos + del.length();
	}
}
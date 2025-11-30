#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

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
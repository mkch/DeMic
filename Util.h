#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <span>
#include <cstddef> // for std::byte
#include <stdexcept>
#include <format>
#include <filesystem>


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

// FromtUTF8 converts a UTF-8 encoded string to a wide string. Returns an empty string if conversion fails.
std::wstring FromUTF8(const char8_t* str, size_t len = (size_t)-1);
std::wstring FromUTF8(const std::u8string& str);
std::wstring FromUTF8(const std::u8string_view& str);
// FromACP converts an string encoded in ANSI code page for the current thread
// to a wide string. Returns an empty string if conversion fails.
std::wstring FromACP(const char* str, size_t len = (size_t)-1);
std::wstring FromACP(const std::string& str);
std::wstring FromACP(const std::string_view& str);
// ToUTF8 converts a wide string to a UTF-8 encoded string. Returns an empty string if conversion fails.
std::u8string ToUTF8(const wchar_t* str, size_t len = (size_t)-1);
std::u8string ToUTF8(const std::wstring& str);
std::u8string ToUTF8(const std::wstring_view& str);
// ToACP converts a wide string to a string encoded in ANSI code page for the current thread.
std::string ToACP(const wchar_t* str, size_t len);
std::string ToACP(const std::wstring& str);
std::string ToACP(const std::wstring_view& str);


class Win32Error : public std::runtime_error {
private:
	static const DWORD FormatMessageFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER 
		| FORMAT_MESSAGE_FROM_SYSTEM 
		| FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK;
public:
	// GetMessageW returns the error message of the given error code.
	// If hModule is given, it also looks up the message in the module.
	inline static std::wstring GetMessageW(DWORD lastError, HMODULE hModule = NULL) {
		wchar_t* msg = NULL;
		FormatMessageW(FormatMessageFlags | (hModule ? FORMAT_MESSAGE_FROM_HMODULE : 0), hModule, lastError, 0, (LPWSTR)&msg, 0, NULL);
		const std::wstring message = std::format(L"{}: {}", lastError, msg ? msg : L"");
		LocalFree(msg);
		return message;
	}
	// GetMessageA returns the error message of the given error code.
	// If hModule is given, it also looks up the message in the module.
	inline static std::string GetMessageA(DWORD lastError, HMODULE hModule = NULL) {
		char* msg = NULL;
		FormatMessageA(FormatMessageFlags | (hModule ? FORMAT_MESSAGE_FROM_HMODULE : 0), hModule, lastError, 0, (LPSTR)&msg, 0, NULL);
		const std::string message = std::format("{}: {}", lastError, msg ? msg : "");
		LocalFree(msg);
		return message;
	}
private:
	DWORD code;
public:
	Win32Error(DWORD errorCode, HMODULE hModule = NULL) : std::runtime_error(GetMessageA(errorCode, hModule)), code(errorCode) {}
	DWORD Code() const { return code; }
};

// LoadModuleResource loads a resource in the module and returns its content as a byte span.
// If the resource is not found, throws a Win32Error.
template<class T>
static std::span<const T> LoadModuleResource(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)) {
	HRSRC hRes = FindResourceExW(hModule, lpType, lpName, wLanguage);
	if (!hRes) {
		throw Win32Error(GetLastError());
	}
	HGLOBAL hResLoad = LoadResource(hModule, hRes);
	if (!hResLoad) {
		throw Win32Error(GetLastError());
	}
	DWORD resSize = SizeofResource(hModule, hRes);
	if (resSize == 0) {
		throw Win32Error(GetLastError());
	}
	void* resData = LockResource(hResLoad);
	if (!resData) {
		throw Win32Error(GetLastError());
	}
	return std::span<const T>(reinterpret_cast<const T*>(resData), resSize / sizeof(T));
}

std::filesystem::path GetModuleFilePath(HMODULE hModule = 0);
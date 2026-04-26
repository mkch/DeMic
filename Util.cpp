#include "Util.h"
#include <sstream>
#include <algorithm>
#include <limits>
#undef max

const std::wstring& StringRes::Load(UINT resId) {
    const auto it = stringResMap.find(resId);
    if (it != stringResMap.end()) {
        return it->second;
    }
    wchar_t* buf = NULL;
    const int n = LoadStringW(hInstance, resId, (LPWSTR)&buf, 0);
    if (n == 0) {
        MessageBoxW(NULL, L"Can't load string resource!", L"DeMic", MB_ICONERROR);
        std::exit(1);
    }
    stringResMap[resId] = std::wstring(buf, n);
    return stringResMap[resId];
}

std::vector<wchar_t> DupCStr(const std::wstring& str) {
    std::vector<wchar_t> r;
    std::for_each(str.begin(), str.end(), [&r](wchar_t c) {
        r.push_back(c);
     });
    r.push_back(0);
    return r;
}

std::wstring FromUTF8(const char8_t* str, size_t len) {
    if (str == nullptr) {
		throw std::invalid_argument("str is null");
    }
    if(len == (size_t)-1) {
        len = std::char_traits<char8_t>::length(str);
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(str), (int)len, NULL, 0);
    if (size_needed <= 0) {
        return L"";
    }
	std::wstring result(size_needed, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(str), (int)len, result.data(), size_needed) == 0) {
        return L"";
    }
    return result;
}

std::wstring FromUTF8(const std::u8string& str) {
    return FromUTF8(str.c_str(), str.size());
}

std::wstring FromUTF8(const std::u8string_view& str) {
    return FromUTF8(str.data(), str.size());
}

std::wstring FromACP(const char* str, size_t len) {
    if (str == nullptr) {
        throw std::invalid_argument("str is null");
    }
    if (len == (size_t)-1) {
        len = std::char_traits<char>::length(str);
    }
    int size_needed = MultiByteToWideChar(CP_THREAD_ACP, 0, reinterpret_cast<const char*>(str), (int)len, NULL, 0);
    if (size_needed <= 0) {
        return L"";
    }
    std::wstring result(size_needed, 0);
    if (MultiByteToWideChar(CP_THREAD_ACP, 0, reinterpret_cast<const char*>(str), (int)len, result.data(), size_needed) == 0) {
        return L"";
    }
    return result;
}

std::wstring FromACP(const std::string& str) {
    return FromACP(str.c_str(), str.size());
}

std::wstring FromACP(const std::string_view& str) {
    return FromACP(str.data(), str.size());
}

std::u8string ToUTF8(const wchar_t* str, size_t len) {
    if (str == nullptr) {
        throw std::invalid_argument("str is null");
    }
    if(len == (size_t)-1) {
        len = wcslen(str);
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, str, (int)len, NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        return u8"";
    }
    std::u8string result(size_needed, 0);
    if (WideCharToMultiByte(CP_UTF8, 0, str, (int)len, (char*)result.data(), size_needed, NULL, NULL) == 0) {
        return u8"";
    }
    return result;
}

std::u8string ToUTF8(const std::wstring& str) {
    return ToUTF8(str.c_str(), str.size());
}

std::u8string ToUTF8(const std::wstring_view& str) {
    return ToUTF8(str.data(), str.size());
}

std::string ToACP(const wchar_t* str, size_t len) {
    if (str == nullptr) {
        throw std::invalid_argument("str is null");
    }
    if (len == (size_t)-1) {
        len = wcslen(str);
    }
    int size_needed = WideCharToMultiByte(CP_ACP, 0, str, (int)len, NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        return "";
    }
    std::string result(size_needed, 0);
    if (WideCharToMultiByte(CP_ACP, 0, str, (int)len, (char*)result.data(), size_needed, NULL, NULL) == 0) {
        return "";
    }
    return result;
}

std::string ToACP(const std::wstring& str) {
    return ToACP(str.c_str(), str.size());
}

std::string ToACP(const std::wstring_view& str) {
    return ToACP(str.data(), str.size());
}

std::filesystem::path GetModuleFilePath(HMODULE hModule) {
	std::wstring path(MAX_PATH, '\0');
	for (;;) {
		DWORD size = GetModuleFileNameW(hModule, path.data(), (DWORD)path.size());
		if (size == 0) {
			throw Win32Error(GetLastError());
		}
		if (size < path.size()) {
			path.resize(size);
			return path;
		}
		// The buffer is too small, increase it and try again.
		const size_t newSize = path.size() * 2;
		if (newSize > 32767) {
			throw Win32Error(ERROR_FILENAME_EXCED_RANGE);
		}
		path.resize(newSize);
	}
}
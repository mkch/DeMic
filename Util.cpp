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

std::wstring FromUTF8(const char* str, size_t len) {
    if (str == nullptr) {
		throw std::invalid_argument("str is null");
    }
    if(len == (size_t)-1) {
        len = strlen(str);
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, (int)len, NULL, 0);
    if (size_needed <= 0) {
        return L"";
    }
    std::vector<wchar_t> buf(size_needed);
    if (MultiByteToWideChar(CP_UTF8, 0, str, (int)len, buf.data(), size_needed) == 0) {
        return L"";
    }
    return std::wstring(buf.data(),  size_needed);
}

std::wstring FromUTF8(const std::string& str) {
    return FromUTF8(str.c_str(), str.size());
}

std::string ToUTF8(const wchar_t* str, size_t len) {
    if (str == nullptr) {
        throw std::invalid_argument("str is null");
    }
    if(len == (size_t)-1) {
        len = wcslen(str);
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, str, (int)len, NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        return "";
    }
    std::vector<char> buf(size_needed);
    if (WideCharToMultiByte(CP_UTF8, 0, str, (int)len, buf.data(), size_needed, NULL, NULL) == 0) {
        return "";
    }
    return std::string(buf.data(), size_needed);
}

std::string ToUTF8(const std::wstring& str) {
    return ToUTF8(str.c_str(), str.size());
}
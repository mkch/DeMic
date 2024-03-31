#include "Util.h"
#include <sstream>
#include <algorithm>
#include <limits>
#undef max

std::wstring utilAppName;

// Extract the base part of a file path.
// E.g. file_path_base(L"C:\\a\\b\\c.txt",'\\') == "c.txt"
static const wchar_t* file_path_base(const wchar_t* path, const wchar_t delim = '\\') {
    if (delim == 0) return path;
    const size_t len = std::wcslen(path);
    if (len == 0) return path;
    auto p = path + len - 1;
    do {
        if (*p == delim) {
            return p+1;
        }
        p--;
    } while (p >= path);
    return path;
}

void ShowError(const wchar_t* msg) {
    MessageBoxW(NULL, msg, utilAppName.c_str(), MB_ICONERROR);
}

void ShowError(const wchar_t* msg, const wchar_t* file, int line) {
    ShowError((std::wstringstream() << file_path_base(file) << L":" << line << L"\n" << msg).str().c_str());
}

// Show a message box with the error description of lastError.
void ShowError(DWORD lastError, const wchar_t* file, int line) {
    wchar_t* msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
    const std::wstring message = (std::wostringstream() << lastError << L": " << (msg ? msg : L"<unknown>")).str();
    LocalFree(msg);
    ShowError(message.c_str(), file, line);
}

const std::wstring& StringRes::Load(UINT resId) {
    const auto it = stringResMap.find(resId);
    if (it != stringResMap.end()) {
        return it->second;
    }
    wchar_t* buf = NULL;
    const int n = LoadStringW(hInstance, resId, (LPWSTR)&buf, 0);
    if (n == 0) {
        SHOW_LAST_ERROR();
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

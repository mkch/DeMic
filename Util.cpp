#include "Util.h"
#include "Log.h"
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

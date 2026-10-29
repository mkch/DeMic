#include "framework.h"
#include "resource.h"
#include <wininet.h>

#include "UpdateChecker.h"
#include "DeMic.h"
#include "Log.h"
#include "nlohmann/json.hpp"

#include <string>
#include <thread>
#include <atomic>


#pragma comment(lib, "wininet.lib")

static std::wstring GetLastInternetError() {
    auto lastError = GetLastError();
    wchar_t* msg = NULL;
    auto hWinInet = GetModuleHandleW(L"wininet.dll");
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        hWinInet, lastError, 0, (LPWSTR)&msg, 0, NULL);
    std::wstring message = (std::wostringstream() << lastError << L": " << (msg ? msg : L"<unknown>")).str();
    LocalFree(msg);

    DWORD code = 0;
    DWORD length = 0;
    if (InternetGetLastResponseInfoW(&code, NULL, &length) && length > 0) {
        std::vector<wchar_t>buf(length);
        if (InternetGetLastResponseInfoW(&code, buf.data(), &length) && length > 0) {
			message = (std::wostringstream() << message << " >> " << code << L": " << std::wstring(buf.data(), length)).str();
        }
    }
    return message;
}

// HTTP_GET fetches the url. 
static bool HTTP_GET(const wchar_t* url, const wchar_t* headers, std::ostream& body, std::atomic<bool>& cancel) {
    HINTERNET hInternet = InternetOpenW(strRes->Load(IDS_APP_TITLE).c_str(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        LOG_ERROR(GetLastInternetError().c_str());
        return false;
    }

    if (cancel) {
        InternetCloseHandle(hInternet);
        return false;
    }

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, headers, 0, INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES, 0);
    if (!hUrl) {
        LOG_ERROR(GetLastInternetError().c_str());
        InternetCloseHandle(hInternet);
        return false;
    }

    if (cancel) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[1024];
    DWORD bytesRead = 0;
    BOOL ok = false;
    while ((ok = InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead)) && bytesRead > 0) {
        if (cancel) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }
        body.write(buffer, bytesRead);
    }
    if (!ok) {
        LOG_ERROR(GetLastInternetError().c_str());
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    const auto okCloseUrl = InternetCloseHandle(hUrl);
    return InternetCloseHandle(hInternet) && okCloseUrl;
}

std::atomic<bool> cancelUpdateCheck = false;
std::thread* updateCheckThread = NULL;

static void StopUpdateCheckThread() {
    cancelUpdateCheck = true;
    if (updateCheckThread) {
        updateCheckThread->join();
        delete updateCheckThread;
        updateCheckThread = NULL;
    }
}


static void StartUpdateCheckThread(HWND hwnd, UINT doneMessage) {
    static const wchar_t* endpoint = L"https://api.github.com/repos/mkch/DeMic/releases/latest";
    static const wchar_t* headers =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2026-03-10\r\n";

    StopUpdateCheckThread();

	cancelUpdateCheck = false;
    updateCheckThread = new std::thread([hwnd, doneMessage]() {
        auto stream = new(std::stringstream);
        bool ok  = HTTP_GET(endpoint, headers, *stream, cancelUpdateCheck);
        // WPARAM: HTTPGet result (bool)
        // LPARAM: HTTPGet response (std::stringstream*). Need delete by receiver.
		PostMessageW(hwnd, doneMessage, ok, (LPARAM)stream);
    });
}

bool CheckingForUpdate() {
	return updateCheckThread != NULL;
}

void CheckForUpdate(HWND hwnd, UINT doneMessage) {
    if (updateCheckThread) {
        // Already checking
		LOG(Logger::LevelInfo, L"Update check already in progress.");
        // WPARAM: 0
        // LPARAM: 0
        PostMessageW(hwnd, doneMessage, 0, 0);
        return;
    }
	StartUpdateCheckThread(hwnd, doneMessage);
}

void CancelUpdateCheck() {
    StopUpdateCheckThread();
}

// No pending checking: wParam is the HTTPGet result(bool), lParam is the response (std::stringstream*).
// Has pending checking: both wParam and lParam are 0.
void OnUpdateCheckDone(WPARAM wParam, LPARAM lParam, 
    std::function<void(const WSemVer& current, const WSemVer& remote, const std::wstring& url)>onDone,
    std::function<void()>onPending, 
    std::function<void()>onError) {

    if (!wParam && !lParam) {
        onPending();
        return;
    }

    struct defer {
        ~defer() {
            StopUpdateCheckThread();
        }
    } _defer;
    // No need to join, PostMessageW synchronizes.
    std::unique_ptr<std::stringstream> body((std::stringstream*)(lParam));
    bool httpGetOk = wParam;
    if (!httpGetOk) {
        if (cancelUpdateCheck) {
            return;
        }
        onError();
        return;
    }

    const auto ver = simulateVersionForUpdateCheck.empty() ? VERSION : simulateVersionForUpdateCheck;
    auto currentVer = parseSemVer(ver);
    if (!currentVer) {
        LOG_ERROR(std::format(L"Failed to parse current version: {}", ver).c_str());
        onError();
        return;
    }

    std::wstring tag, url;
    try {
        const auto response = nlohmann::json::parse(*body);
        const std::string tagName = response.at("tag_name");
        tag = FromUTF8(std::u8string_view((const char8_t*)tagName.data(), tagName.length()));
        if (tag.length() > 0 && tag[0] == L'v') {
            tag = tag.substr(1);
        }
        const std::string htmlURL = response.at("html_url");
        url = FromUTF8(std::u8string_view((const char8_t*)htmlURL.data(), htmlURL.length()));
    } catch (...) {
        onError();
        return;
    }

    auto const remoteVer = parseSemVer(tag, false);
    if (!remoteVer) {
        LOG_ERROR(std::format(L"Failed to parse remote version: {}", tag).c_str());
        onError();
        return;
    }
    onDone(*currentVer, *remoteVer, url);
    return;
}


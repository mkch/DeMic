// DeMic.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "resource.h"
#include "DeMic.h"
#include <Commctrl.h>
#include <Dbt.h>
#include <string>
#include <map>
#include <strsafe.h>

#include "MicCtrl.h"

#define MAX_LOADSTRING 1024

// Notify message used by Shell_NotifyIconW.
static const UINT UM_NOTIFY = WM_USER + 1;
static const int HOTKEY_ID = 1;

static const wchar_t* const CONFIG_FILE_NAME = L"DeMic.ini";

#define _WT(str) L""##str
// Should be:
// #define W__FILE__ L""##__FILE__
// But the linter of VSCode does not like it(bug?).
#define W__FILE__ _WT(__FILE__)

#define SHOW_ERROR(err) ShowError(err, W__FILE__, __LINE__)
#define SHOW_LAST_ERROR() SHOW_ERROR(GetLastError())

void ShowError(const wchar_t* msg);
void ShowError(const wchar_t* msg, const wchar_t* file, int line);
void ShowError(DWORD lastError, const wchar_t* file, int line);
const std::wstring& LoadStringRes(UINT resId);
void ShowNotification(HWND hwnd, bool silent);
void UpdateNotification(HWND hwnd);
void RemoveNotification(HWND hwnd);
void ReadConfig();
void WriteConfig();
bool StartOnBootEnabled();
void EnableStartOnBoot();
void DisableStartOnBoot();
bool ResetHotKey();
static bool AlreadyRunning();

// Global Variables:
HINSTANCE hInst;                                // current instance
std::wstring appTitle;                          // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND mainWindow = NULL;
HWND hotKeySettingWindow = NULL;

BYTE hotKeyVk = 0; // Current hotkey vk of the hotkey contorl.
BYTE hotKeyMod = 0;// Current hotkey modifier of the hotkey control.

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK HotKeySetting(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

MicCtrl micCtrl;
bool micMuted = false;
std::wstring configFilePath, startOnBootCmd;
bool silentMode = false;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    if (AlreadyRunning()) {
        MessageBoxW(NULL, LoadStringRes(IDS_ALREADY_RUNNING).c_str(), LoadStringRes(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 1;
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLine(), &argc);

    if (argc > 1) {
        const auto argv1 = std::wstring(argv[1]);
        silentMode = (argv1 == L"/silent" || argv1 == L"-silent");
    }

    // Initialize configFilePath.
    std::wstring moduleFilePath = argv[0];
    configFilePath = moduleFilePath;
    const auto sep = configFilePath.rfind(L'\\');
    if (sep != std::wstring::npos) {
        configFilePath = configFilePath.substr(0, sep + 1) + CONFIG_FILE_NAME;
    }
    startOnBootCmd = std::wstring(L"\"") + moduleFilePath + L"\" /silent";

    micCtrl.Init();

    // Initialize global strings
    WCHAR szTitle[MAX_LOADSTRING]={0};
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    appTitle = &szTitle[0];
    LoadStringW(hInstance, IDC_DEMIC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    ReadConfig();
    ResetHotKey();

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DEMIC));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (IsDialogMessage(hotKeySettingWindow, &msg)) {
            continue;
        }
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DEMIC));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DEMIC);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_DEMIC));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   mainWindow = CreateWindowW(szWindowClass, appTitle.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!mainWindow) {
      return FALSE;
   }

   hotKeySettingWindow = CreateDialog(hInst, MAKEINTRESOURCE(IDD_HOTKEY_SETTING), mainWindow, HotKeySetting);
   if (!hotKeySettingWindow) {
       return FALSE;
   }

   //ShowWindow(mainWindow, nCmdShow);
   //UpdateWindow(mainWindow);

   return TRUE;
}

// Timer id for delaying the WM_DEVICECHANGE message processing.
static const UINT_PTR DELAY_DEVICE_CHANGE_TIMER = 1;
// Batch interval of  WM_DEVICECHANGE message processing.
static const UINT DEVICE_CHANGE_DELAY = 1000;

void CALLBACK DelayDeviceChangeTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    KillTimer(hwnd, DELAY_DEVICE_CHANGE_TIMER);  // Make it one time timer.
    micCtrl.ReloadDevices();
    UpdateNotification(hwnd);
}

void ShowHotKeySettingWindow() {
    HWND hotKey = GetDlgItem(hotKeySettingWindow, IDC_HOTKEY);
    WPARAM wParam = MAKELONG(MAKEWORD(hotKeyVk, hotKeyMod), 0);
    SendMessage(hotKey, HKM_SETHOTKEY, wParam, 0);
    ShowWindow(hotKeySettingWindow, SW_SHOW);
}
void ProcessNotifyMenuCmd(HWND hWnd, UINT_PTR cmd) {
    switch (cmd) {
    case ID_MENU_HOTKEYSETTING:
        ShowHotKeySettingWindow();
        break;
    case ID_MENU_START_ON_BOOT:
        if (StartOnBootEnabled()) {
            DisableStartOnBoot();
        }
        else {
            EnableStartOnBoot();
        }
        break;
    case ID_MENU_EXIT:
        SendMessage(mainWindow, WM_CLOSE, 0, 0);
        break;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        ShowNotification(hWnd, silentMode);
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            micCtrl.SetMuted(!micCtrl.GetMuted());
        }
        break;
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVNODES_CHANGED) {
            if (!SetTimer(hWnd, DELAY_DEVICE_CHANGE_TIMER, DEVICE_CHANGE_DELAY, DelayDeviceChangeTimerProc)) {
                SHOW_LAST_ERROR();
            }
        }
        break;
    case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId) {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case UM_NOTIFY:
        if (lParam == WM_RBUTTONUP) {
            SetForegroundWindow(hWnd);
            HMENU menu = LoadMenu(hInst, MAKEINTRESOURCE(IDC_NOTIF_MENU));
            menu = GetSubMenu(menu, 0);
            CheckMenuItem(menu, ID_MENU_START_ON_BOOT, MF_BYCOMMAND | (StartOnBootEnabled()? MF_CHECKED : MF_UNCHECKED));
            POINT pt = { 0 };
            GetCursorPos(&pt);
            UINT_PTR cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
                pt.x, pt.y,
                0,
                hWnd, NULL);
            DestroyMenu(menu);
            if (cmd != 0) {
                ProcessNotifyMenuCmd(hWnd, cmd);
            }
        } else if (lParam == WM_LBUTTONUP) {
            micCtrl.SetMuted(!micCtrl.GetMuted());
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code that uses hdc here...
        EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        RemoveNotification(hWnd);
        UnregisterHotKey(mainWindow, HOTKEY_ID);
        PostQuitMessage(0);
        break;
    case MicCtrl::WM_MUTED_STATE_CHANGED:
        OutputDebugString(wParam == 1 ? L"Muted\n" : L"Unmuted\n");
        UpdateNotification(hWnd);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// ResetHotKey resets the hot key.
bool ResetHotKey() {
    UnregisterHotKey(mainWindow, HOTKEY_ID);
    if (hotKeyVk == 0) { // No hot key is given.
        return true;
    }
    // Translate modifier.
    UINT modifier = 0;
    if (hotKeyMod & HOTKEYF_ALT) {
        modifier |= MOD_ALT;
    }
    if (hotKeyMod & HOTKEYF_CONTROL) {
        modifier |= MOD_CONTROL;
    }
    if (hotKeyMod & HOTKEYF_SHIFT) {
        modifier |= MOD_SHIFT;
    }
    
    if (!RegisterHotKey(mainWindow, HOTKEY_ID, modifier, hotKeyVk)) {
        DWORD lastError = GetLastError();
        if (lastError == 1409) { // 1409: ERROR_HOTKEY_ALREADY_REGISTERED
            wchar_t* msg = NULL;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
            ShowError(msg);
            LocalFree(msg);
        }
        else {
            SHOW_LAST_ERROR();
        }
        return false;
    }
    return true;
}

// Message handler for about box.
INT_PTR CALLBACK HotKeySetting(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) > 0) { // The window is actived.
            // Focus the hotkey control.
            SetFocus(GetDlgItem(hDlg, IDC_HOTKEY));
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
                // Get the hot key setting of hot key control.
                HWND hotKeyCtrl = GetDlgItem(hDlg, IDC_HOTKEY);
                auto hk = SendMessage(hotKeyCtrl, HKM_GETHOTKEY, 0, 0);
                hotKeyVk = LOBYTE(LOWORD(hk));
                hotKeyMod = HIBYTE(LOWORD(hk));
                // Reset the system hot key.
                if (!ResetHotKey()) {
                    // Clear hot key values if unable to register the hot key.
                    hotKeyVk = 0;
                    hotKeyMod = 0;
                    // Clear the hot key control.
                    SendMessage(hotKeyCtrl, HKM_SETHOTKEY, 0, 0);
                    SetFocus(hotKeyCtrl);
                    break;
                }
                WriteConfig();
            }
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void ShowError(const wchar_t* msg) {
    MessageBoxW(NULL, msg, LoadStringRes(IDS_APP_TITLE).c_str(), MB_ICONERROR);
}

void ShowError(const wchar_t* msg, const wchar_t* file, int line) {
    wchar_t message[1024] = { 0 };
    StringCbPrintfW(message, sizeof message, L"%s:%d\n%s", file, line, msg);
    ShowError(message);
}

// Show a message box with the error description of lastError.
void ShowError(DWORD lastError, const wchar_t* file, int line) {
    wchar_t* msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
    ShowError(msg, file, line);
    LocalFree(msg);
}

static std::map<UINT, std::wstring> stringResMap;
const std::wstring& LoadStringRes(UINT resId) {
    const auto it = stringResMap.find(resId);
    if (it != stringResMap.end()) {
        return it->second;
    }
    wchar_t* buf = NULL;
    const int n = LoadStringW(hInst, resId, (LPWSTR)&buf, 0);
    if (n == 0) {
        SHOW_LAST_ERROR();
        std::exit(1);
    }
    stringResMap[resId] = std::wstring(buf, n);
    return stringResMap[resId];
}

// ID of Shell_NotifyIconW.
static const UINT NOTIFY_ID = 1;

void ShowNotificationImpl(HWND hwnd, bool modify, bool silent) {
    NOTIFYICONDATAW data = { 0 };
    data.cbSize = sizeof data;
    data.hWnd = hwnd;
    data.uID = NOTIFY_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    if (!silent) {
        data.uFlags |= NIF_INFO;
        StringCbCopyW(data.szInfo, sizeof(data.szInfo), LoadStringRes(IDS_RUNNING_IN_SYSTEM_TRAY).c_str());
        StringCbCopyW(data.szInfoTitle, sizeof(data.szInfoTitle), LoadStringRes(IDS_APP_TITLE).c_str());
        data.dwInfoFlags = NIIF_INFO;
    }
    data.uCallbackMessage = UM_NOTIFY;
    data.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(micCtrl.GetMuted() ? IDI_MICROPHONE_MUTED : IDI_MICPHONE));
    StringCchCopyW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], LoadStringRes(IDS_APP_TITLE).c_str());
    if (!Shell_NotifyIconW(modify ? NIM_MODIFY : NIM_ADD, &data)) {
        SHOW_LAST_ERROR();
        return;
    }
}

void ShowNotification(HWND hwnd, bool silent) {
    ShowNotificationImpl(hwnd, false, silent);
}

void UpdateNotification(HWND hwnd) {
    ShowNotificationImpl(hwnd, true, true);
}

void RemoveNotification(HWND hwnd) {
    NOTIFYICONDATAW data = { 0 };
    data.cbSize = sizeof data;
    data.hWnd = hwnd;
    data.uID = NOTIFY_ID;
    if (!Shell_NotifyIconW(NIM_DELETE, &data)) {
        SHOW_LAST_ERROR();
        return;
    }
}

// ini section name.
static const auto CONFIG_HOTKEY = L"HotKey";
// ini key.
static const auto CONFIG_VALUE = L"Value";

// Read settings from config file.
void ReadConfig() {
    if (!PathFileExistsW(configFilePath.c_str())) {
        return;
    }
    const int value = GetPrivateProfileIntW(CONFIG_HOTKEY, CONFIG_VALUE, 0, configFilePath.c_str());
    hotKeyVk = LOBYTE(LOWORD(value));
    hotKeyMod = HIBYTE(LOWORD(value));
}

// Write settings to config file.
void WriteConfig() {
    WritePrivateProfileStringW(CONFIG_HOTKEY, CONFIG_VALUE,
        std::to_wstring(MAKEWORD(hotKeyVk, hotKeyMod)).c_str(),
        configFilePath.c_str());
}

static const auto START_ON_BOOT_REG_SUB_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const auto START_ON_BOOT_REG_VALUE_NAME = L"DeMic";

bool StartOnBootEnabled() {
    wchar_t value[1024] = { 0 };
    DWORD read = sizeof(value);
    const auto ret = RegGetValueW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY, START_ON_BOOT_REG_VALUE_NAME,
        RRF_RT_REG_SZ, NULL,
        value, &read);
    if (ret != ERROR_SUCCESS) {
        return false;
    }
    else {
        auto equal =
            std::equal(startOnBootCmd.begin(), startOnBootCmd.end(),
                value, value + read / sizeof(wchar_t) - 1,
                [](auto a, auto b) { return tolower(a) == tolower(b); });
        return equal;
    }
}

void EnableStartOnBoot() {
    HKEY key = NULL;
    auto ret = RegOpenKeyExW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY,
        0, KEY_WRITE,
        &key);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
    }
    else {
        ret = RegSetValueExW(key, START_ON_BOOT_REG_VALUE_NAME,
            0, REG_SZ,
            (const BYTE*)startOnBootCmd.c_str(), DWORD(startOnBootCmd.length()) * sizeof(wchar_t));
        if (ret != ERROR_SUCCESS) {
            SHOW_ERROR(ret);
        }
    }
    RegFlushKey(key);
    RegCloseKey(key);
}

void DisableStartOnBoot() {
    HKEY key = NULL;
    auto ret = RegOpenKeyExW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY,
        0, KEY_WRITE,
        &key);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
    }
    else {
        ret = RegDeleteValueW(key, START_ON_BOOT_REG_VALUE_NAME);
        if (ret != ERROR_SUCCESS) {
            SHOW_ERROR(ret);
        }
    }
    RegFlushKey(key);
    RegCloseKey(key);
}

static const auto RUNNING_MUTEX_NAME = L"DeMic is running";
static bool AlreadyRunning() {
    if (CreateMutexW(NULL, FALSE, RUNNING_MUTEX_NAME) == NULL) {
        SHOW_LAST_ERROR();
        std::exit(1);
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
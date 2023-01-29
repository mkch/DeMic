// DeMic.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "resource.h"
#include "DeMic.h"
#include <Commctrl.h>
#include <shlwapi.h>
#include <Dbt.h>
#include <string>
#include <strsafe.h>
#include <windowsx.h>

#include "Plugin.h"

// Currrent version of DeMic.
static const wchar_t* VERSION = L"0.4";

#define MAX_LOADSTRING 1024

// Notify message used by Shell_NotifyIconW.
static const UINT UM_NOTIFY = WM_USER + 1;
static const int HOTKEY_ID = 1;

// Microphone command message used by command line args.
static const UINT UM_MIC_CMD = WM_USER + 2;

static const wchar_t* const CONFIG_FILE_NAME = L"DeMic.ini";

StringRes* strRes = NULL;

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
void PlayOnSound(const std::wstring&);
void PlayOffSound(const std::wstring&);
BOOL devFilter(const wchar_t* devID);

// Global Variables:
HINSTANCE hInst;                                    // current instance
std::wstring appTitle;                              // The title bar text
LPCWSTR szWindowClass = L"github.com/mkch/DeMic";   // the main window class name
HWND mainWindow = NULL;
HWND hotKeySettingWindow = NULL;
HWND soundSettingsWindow = NULL;

BYTE hotKeyVk = 0; // Current hotkey vk of the hotkey contorl.
BYTE hotKeyMod = 0; // Current hotkey modifier of the hotkey control.

BOOL enableOnSound = FALSE; // Enable mic on notification sound.
std::wstring onSoundPath; // The mic on notification sound file.
BOOL enableOffSound = FALSE; // Enable mic off notification sound.
std::wstring offSoundPath; // The sound file.

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK HotKeySetting(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SoundSettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

MicCtrl micCtrl;
bool micMuted = false;
std::wstring configFilePath, startOnBootCmd;
bool silentMode = false;

HMENU popupMenu = NULL;
HMENU pluginMenu = NULL;

// Command line args of microphone commands.
enum MIC_CMD {
    CMD_NONE,   // No command.
    CMD_ON,     // Turn on microphone.
    CMD_OFF,    // Turn off microphone.
    CMD_TOGGLE, // Toggle on/off.
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    micCtrl.SetDevFilter(devFilter);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLine(), &argc);

    MIC_CMD  cmd = CMD_NONE;

    if (argc > 1) {
        const auto argv1 = std::wstring(argv[1]);
        if (argv1 == L"/silent" || argv1 == L"-silent") {
            silentMode = true;
        } else if (argv1 == L"/on" || argv1 == L"-on") {
            silentMode = true;
            cmd = CMD_ON;
        } else if (argv1 == L"/off" || argv1 == L"-off") {
            silentMode = true;
            cmd = CMD_OFF;
        } else if (argv1 == L"/toggle" || argv1 == L"-toggle") {
            silentMode = true;
            cmd = CMD_TOGGLE;
        }
    }

    // Initialize global strings
    WCHAR szTitle[MAX_LOADSTRING] = { 0 };
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    appTitle = &szTitle[0];
    utilAppName = appTitle;
    strRes = new StringRes(hInstance);

    if (AlreadyRunning()) {
        if (cmd != CMD_NONE) {
            // Find the main window of running process.
            const HWND hwnd = FindWindowW(szWindowClass, appTitle.c_str());
            if (hwnd != NULL) {
                PostMessage(hwnd, UM_MIC_CMD, cmd, 0);
                return 0;
            }
        }
        MessageBoxW(NULL, strRes->Load(IDS_ALREADY_RUNNING).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 1;
    }


    // Initialize configFilePath.
    std::wstring moduleFilePath = argv[0];
    configFilePath = moduleFilePath;
    const auto sep = configFilePath.rfind(L'\\');
    if (sep != std::wstring::npos) {
        configFilePath = configFilePath.substr(0, sep + 1) + CONFIG_FILE_NAME;
    }
    startOnBootCmd = std::wstring(L"\"") + moduleFilePath + L"\" /silent";

    if (!MyRegisterClass(hInstance)) {
        SHOW_LAST_ERROR();
        return FALSE;
    }

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    ReadConfig();
    ResetHotKey();
    LoadPlugins();

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DEMIC));

    MSG msg;

    // Process microphone comands from command line.
    if (cmd != CMD_NONE) {
        PostMessage(mainWindow, UM_MIC_CMD, cmd, 0);
    }

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (IsDialogMessage(hotKeySettingWindow, &msg) || 
            IsDialogMessage(soundSettingsWindow, &msg)) {
            continue;
        }
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

BOOL devFilter(const wchar_t* devID) {
    return CallPluginDevFilter(devID);
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

   soundSettingsWindow = CreateDialog(hInst, MAKEINTRESOURCE(IDD_SOUND_SETTINGS), mainWindow, SoundSettings);
   if (!soundSettingsWindow) {
       return FALSE;
   }

   // This menu is never destroyed via my code.
   const HMENU menu = LoadMenu(hInst, MAKEINTRESOURCE(IDC_NOTIF_MENU));
   popupMenu = GetSubMenu(menu, 0);
   pluginMenu = CreatePopupMenu();
   MENUITEMINFOW menuInfo = { sizeof(menuInfo), MIIM_SUBMENU, 0, 0, 0, pluginMenu, 0 };
   if (!SetMenuItemInfoW(popupMenu, ID_MENU_PLUGIN, FALSE, &menuInfo)) {
       SHOW_LAST_ERROR();
   }

   //ShowWindow(mainWindow, nCmdShow);
   //UpdateWindow(mainWindow);

   return TRUE;
}

// Timer id for delaying the MicCtrl::WM_DEVICE_STATE_CHANGED message processing.
static const UINT_PTR DELAY_DEVICE_CHANGE_TIMER = 1;
// Batch interval of  WM_DEVICECHANGE message processing.
static const UINT DEVICE_CHANGE_DELAY = 1000;

void CALLBACK DelayDeviceChangeTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    KillTimer(hwnd, DELAY_DEVICE_CHANGE_TIMER);  // Make it one time timer.
    micCtrl.ReloadDevices();
    UpdateNotification(hwnd);
    CallPluginMicStateListeners();
}

void ShowHotKeySettingWindow() {
    HWND hotKey = GetDlgItem(hotKeySettingWindow, IDC_HOTKEY);
    WPARAM wParam = MAKELONG(MAKEWORD(hotKeyVk, hotKeyMod), 0);
    SendMessage(hotKey, HKM_SETHOTKEY, wParam, 0);
    ShowWindow(hotKeySettingWindow, SW_SHOW);
}

void ShowSoundSettingsWindow() {
    HWND onEnable = GetDlgItem(soundSettingsWindow, IDC_ENABLE_ON_SOUND);
    HWND onPath = GetDlgItem(soundSettingsWindow, IDC_ON_SOUND_PATH);
    HWND onPathSelect = GetDlgItem(soundSettingsWindow, IDC_ON_SOUND_SELECT);
    HWND onPlay = GetDlgItem(soundSettingsWindow, IDC_ON_SOUND_PLAY);

    HWND offEnable = GetDlgItem(soundSettingsWindow, IDC_ENABLE_OFF_SOUND);
    HWND offPath = GetDlgItem(soundSettingsWindow, IDC_OFF_SOUND_PATH);
    HWND offPathSelect = GetDlgItem(soundSettingsWindow, IDC_OFF_SOUND_SELECT);
    HWND offPlay = GetDlgItem(soundSettingsWindow, IDC_OFF_SOUND_PLAY);

    Button_SetCheck(onEnable, enableOnSound);
    SetWindowTextW(onPath, onSoundPath.empty() ? strRes->Load(IDS_NAN).c_str() : onSoundPath.c_str());
    SetWindowTextW(offPath, offSoundPath.empty() ? strRes->Load(IDS_NAN).c_str() : offSoundPath.c_str());

    Button_SetCheck(offEnable, enableOffSound);

    ShowWindow(soundSettingsWindow, SW_SHOW);
}

void ProcessNotifyMenuCmd(HWND hWnd, UINT_PTR cmd) {
    switch (cmd) {
    case ID_MENU_HOTKEYSETTING:
        ShowHotKeySettingWindow();
        break;
    case ID_MENU_SOUND_SETTINGS:
        ShowSoundSettingsWindow();
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
    case ID_NO_PLUGIN:
        MessageBoxW(hWnd, (strRes->Load(IDS_PLUGIN_INSTRUCTION) + GetPluginDir()).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        break;
    default:
        if (cmd >= APS_NextPluginCmdID) {
            OnPluginMenuItemCmd((UINT)cmd);
            break;
        }
        ProcessPluginMenuCmd((UINT)cmd);
        break;
    }
}

void PlaySoundFile(LPCWSTR path) {
    PlaySoundW(path, NULL,
        SND_FILENAME | SND_NODEFAULT | SND_ASYNC | SND_SENTRY | SND_SYSTEM);
}

void PlaySystemSound(DWORD sndID) {
    PlaySound((LPCTSTR)(size_t)sndID, NULL, SND_ALIAS_ID | SND_NODEFAULT | SND_ASYNC | SND_SENTRY | SND_SYSTEM);
}

void PlayOnSound() {
    if (enableOnSound) {
        PlayOnSound(onSoundPath);
    }
}

void PlayOffSound() {
    if (enableOffSound) {
        PlayOffSound(offSoundPath);
    }
}

void TurnOnMic() {
    if (micCtrl.GetMuted()) {
        micCtrl.SetMuted(false);
        PlayOnSound();
    }
}

void TurnOffMic() {
    if (!micCtrl.GetMuted()) {
        micCtrl.SetMuted(true);
        PlayOffSound();
    }
}

void ToggleMuted() {
    const auto muted = micCtrl.GetMuted();
    if (muted) {
        PlayOnSound();
    } else {
        PlayOffSound();
    }
    micCtrl.SetMuted(!muted);
}

BOOL IsMuted() {
    return micCtrl.GetMuted();
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case UM_MIC_CMD:
        switch (wParam) {
            case CMD_ON:
                TurnOnMic();
                break;
            case CMD_OFF:
                TurnOffMic();
                break;
            case CMD_TOGGLE:
                ToggleMuted();
                break;
        }
        break;
    case WM_CREATE:
        ShowNotification(hWnd, silentMode);
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            ToggleMuted();
        }
        break;
    case MicCtrl::WM_DEVICE_STATE_CHANGED:
        if (!SetTimer(hWnd, DELAY_DEVICE_CHANGE_TIMER, DEVICE_CHANGE_DELAY, DelayDeviceChangeTimerProc)) {
            SHOW_LAST_ERROR();
        }
        break;
    case MicCtrl::WM_DEFAULT_DEVICE_CHANGED:
        CallPluginDefaultDevChangedListeners();
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
            CheckMenuItem(popupMenu, ID_MENU_START_ON_BOOT, MF_BYCOMMAND | (StartOnBootEnabled()? MF_CHECKED : MF_UNCHECKED));
            POINT pt = { 0 };
            GetCursorPos(&pt);
            UINT_PTR cmd = TrackPopupMenu(popupMenu,
                TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
                pt.x, pt.y,
                0,
                hWnd, NULL);
            if (cmd != 0) {
                ProcessNotifyMenuCmd(hWnd, cmd);
            }
        } else if (lParam == WM_LBUTTONUP) {
            ToggleMuted();
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
        UpdateNotification(hWnd);
        CallPluginMicStateListeners();
        break;
    case WM_INITMENUPOPUP:
        if ((HMENU)wParam == popupMenu) {
            CallPluginInitMenuPopupListener(NULL);
        } else {
            if ((HMENU)wParam == pluginMenu) {
                OnPluginMenuInitPopup();
            }
            CallPluginInitMenuPopupListener((HMENU)wParam);
        }
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

// Message handler for hotkey setting box.
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

bool SelectSoundFile(HWND owner, std::wstring& path) {
    std::wstring winMediaDir;
    wchar_t winDir[MAX_PATH] = { 0 };
    const UINT r = GetWindowsDirectoryW(winDir, MAX_PATH);
    if (r > 0 && r < MAX_PATH) {
        winMediaDir = std::wstring(winDir) + L"\\Media";
    }

    wchar_t buf[1024] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"*.wav\0*.wav\0*.*\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(buf[0]);
    ofn.lpstrInitialDir = winMediaDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) {
        DWORD err = GetLastError();
        if (err != 0) {
            SHOW_ERROR(err);
        }
        return false;
    }
    path = buf;
    return true;
}

void PlayOnSound(const std::wstring& path) {
    if (path.empty()) {
        // Play default.
        PlaySystemSound(SND_ALIAS_SYSTEMDEFAULT);
    } else {
        PlaySoundFile(path.c_str());
    }
}

void PlayOffSound(const std::wstring& path) {
    if (path.empty()) {
        // Play default.
        PlaySystemSound(SND_ALIAS_SYSTEMHAND);
    }
    else {
        PlaySoundFile(path.c_str());
    }
}

// Message handler for sound settings box.
INT_PTR CALLBACK SoundSettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    wchar_t buf[1024] = { 0 };
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ON_SOUND_PATH:
            if (HIWORD(wParam) == STN_DBLCLK) {
                SetWindowTextW((HWND)lParam, strRes->Load(IDS_NAN).c_str());
            }
            break;
        case IDC_OFF_SOUND_PATH:
            if (HIWORD(wParam) == STN_DBLCLK) {
                SetWindowTextW((HWND)lParam, strRes->Load(IDS_NAN).c_str());
            }
            break;
        case IDC_ON_SOUND_SELECT:{
            PlaySoundFile(NULL); // Giving a chance to stop playing.
            std::wstring path;
            if (!SelectSoundFile(hDlg, path)) {
                break;
            }
            SetWindowTextW(GetDlgItem(hDlg, IDC_ON_SOUND_PATH), path.empty() ? strRes->Load(IDS_NAN).c_str() : path.c_str());
            break;
        }
        case IDC_OFF_SOUND_SELECT: {
            PlaySoundFile(NULL); // Giving a chance to stop playing.
            std::wstring path;
            if (!SelectSoundFile(hDlg, path)) {
                break;
            }
            SetWindowTextW(GetDlgItem(hDlg, IDC_OFF_SOUND_PATH), path.empty() ? strRes->Load(IDS_NAN).c_str() : path.c_str());
            break;
        }
        case IDC_ON_SOUND_PLAY: {
            buf[0] = 0;
            GetWindowTextW(GetDlgItem(hDlg, IDC_ON_SOUND_PATH), buf, sizeof(buf) / sizeof(buf[0]));
            PlayOnSound(std::wstring(strRes->Load(IDS_NAN) == buf ? L"" : buf));
            break;
        }
        case IDC_OFF_SOUND_PLAY:{}
            buf[0] = 0;
            GetWindowTextW(GetDlgItem(hDlg, IDC_OFF_SOUND_PATH), buf, sizeof(buf) / sizeof(buf[0]));
            PlayOffSound(std::wstring(strRes->Load(IDS_NAN) == buf ? L"" : buf));
            break;
        case IDOK: {
            enableOnSound = Button_GetCheck(GetDlgItem(hDlg, IDC_ENABLE_ON_SOUND));
            buf[0] = 0;
            GetWindowTextW(GetDlgItem(hDlg, IDC_ON_SOUND_PATH), buf, sizeof(buf) / sizeof(buf[0]));
            onSoundPath = strRes->Load(IDS_NAN) == buf ? L"" : buf;
            
            enableOffSound = Button_GetCheck(GetDlgItem(hDlg, IDC_ENABLE_OFF_SOUND));
            buf[0] = 0;
            GetWindowTextW(GetDlgItem(hDlg, IDC_OFF_SOUND_PATH), buf, sizeof(buf) / sizeof(buf[0]));
            offSoundPath = strRes->Load(IDS_NAN) == buf ? L"" : buf;

            WriteConfig();
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
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
        StringCbCopyW(data.szInfo, sizeof(data.szInfo), strRes->Load(IDS_RUNNING_IN_SYSTEM_TRAY).c_str());
        StringCbCopyW(data.szInfoTitle, sizeof(data.szInfoTitle), (strRes->Load(IDS_APP_TITLE) + L" " + VERSION).c_str());
        data.dwInfoFlags = NIIF_INFO;
    }
    data.uCallbackMessage = UM_NOTIFY;
    data.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(micCtrl.GetMuted() ? IDI_MICROPHONE_MUTED : IDI_MICPHONE));
    wnsprintfW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], strRes->Load(IDS_NOTIFICATION_TIP).c_str(), VERSION);
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

static const auto CONFIG_SOUND = L"Sound";
static const auto CONFIG_ON_ENABLE = L"OnEnable";
static const auto CONFIG_ON_PATH = L"OnPath";
static const auto CONFIG_OFF_ENABLE = L"OffEnable";
static const auto CONFIG_OFF_PATH = L"OffPath";
static const auto CONFIG_PLUGIN = L"Plugin";
// Delimiter of plugin files.
static const auto CONFIG_PLUGIN_DEL = L"|";

// Read settings from config file.
void ReadConfig() {
    if (!PathFileExistsW(configFilePath.c_str())) {
        return;
    }
    const int value = GetPrivateProfileIntW(CONFIG_HOTKEY, CONFIG_VALUE, 0, configFilePath.c_str());
    hotKeyVk = LOBYTE(LOWORD(value));
    hotKeyMod = HIBYTE(LOWORD(value));

    enableOnSound = GetPrivateProfileIntW(CONFIG_SOUND, CONFIG_ON_ENABLE, 0, configFilePath.c_str());
    wchar_t buf[1024] = { 0 };
    GetPrivateProfileStringW(CONFIG_SOUND, CONFIG_ON_PATH, L"", buf, sizeof(buf)/sizeof(buf[0]), configFilePath.c_str());
    onSoundPath = buf;

    enableOffSound = GetPrivateProfileIntW(CONFIG_SOUND, CONFIG_OFF_ENABLE, 0, configFilePath.c_str());
    buf[0] = 0;
    GetPrivateProfileStringW(CONFIG_SOUND, CONFIG_OFF_PATH, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
    offSoundPath = buf;

    buf[0] = 0;
    GetPrivateProfileStringW(CONFIG_PLUGIN, CONFIG_PLUGIN, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
    Split(buf, CONFIG_PLUGIN_DEL, [](const auto& plugin) { configuredPluginFiles.insert(plugin); });
}

// Write settings to config file.
void WriteConfig() {
    WritePrivateProfileStringW(CONFIG_HOTKEY, CONFIG_VALUE,
        std::to_wstring(MAKEWORD(hotKeyVk, hotKeyMod)).c_str(),
        configFilePath.c_str());
    WritePrivateProfileStringW(CONFIG_SOUND, CONFIG_ON_ENABLE,
        std::to_wstring(enableOnSound).c_str(),
        configFilePath.c_str());
    WritePrivateProfileStringW(CONFIG_SOUND, CONFIG_OFF_ENABLE,
        std::to_wstring(enableOffSound).c_str(),
        configFilePath.c_str());
    WritePrivateProfileStringW(CONFIG_SOUND, CONFIG_ON_PATH,
        onSoundPath.c_str(),
        configFilePath.c_str());
    WritePrivateProfileStringW(CONFIG_SOUND, CONFIG_OFF_PATH,
        offSoundPath.c_str(),
        configFilePath.c_str());
    WritePrivateProfileStringW(CONFIG_PLUGIN, CONFIG_PLUGIN,
        Join(configuredPluginFiles.begin(), configuredPluginFiles.end(), CONFIG_PLUGIN_DEL).c_str(),
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
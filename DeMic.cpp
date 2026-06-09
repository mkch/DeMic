// DeMic.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "resource.h"
#include "DeMic.h"
#include <Commctrl.h>
#include <shlwapi.h>
#include <Dbt.h>
#include <string>
#include <array>
#include <fstream>
#include <strsafe.h>
#include <windowsx.h>
#include <shlobj.h>

#include "Plugin.h"
#include "TimeDebouncer.h"
#include "Log.h"

#include "UpdateChecker.h"
#include "HotKeyControlInfo.h"
#include "TaskScheduler.h"
#include "InstanceLock.h"


std::unique_ptr<InstanceLock> instanceLock;

enum {
    // Notify message used by Shell_NotifyIconW.
    UM_NOTIFY = WM_USER + 1,
    // Microphone command message used by command line args.
    UM_MIC_CMD,
    // Done message used by UpdateChecker.
    UM_UPDATE_CHECK_DONE,
};
static const int HOTKEY_ID = 1;



static const wchar_t* const CONFIG_FILE_NAME = L"DeMic.ini";
const static wchar_t* const LOG_FILE_NAME = L"Log.txt";
// Full path of this exe.
const std::wstring moduleFilePath = GetModuleFilePath(); 
// User SID of current process.
const std::wstring processSID = GetCurrentUserSid();
// Whether the current process is elevated.
const bool processElevated = IsCurrentProcessElevated();

std::unique_ptr <StringRes> strRes;

void ShowNotification(HWND hwnd, bool silent);
void UpdateNotification(HWND hwnd);
void RemoveNotification(HWND hwnd);
void ReadConfig();
void WriteConfig();
bool StartOnBootEnabled_Reg();
LogonTaskStatus StartOnBootStatus();
void EnableStartOnBoot_Reg();
bool EnableStartOnBoot(bool asAdmin);
void DisableStartOnBoot_Reg();
bool DisableStartOnBoot();
bool RelaunchAsAdmin();
bool ResetHotKey(HWND hwnd);
static bool AlreadyRunning();
static bool IsAnotherInstanceRunning();
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

HotKeyControlInfo hotKeyInfo;

BOOL enableOnSound = FALSE; // Enable mic on notification sound.
std::wstring onSoundPath; // The mic on notification sound file.
BOOL enableOffSound = FALSE; // Enable mic off notification sound.
std::wstring offSoundPath; // The sound file.

std::wstring cmdLineArgs; // The command line args to use when starting the first instance.
std::wstring cmdLineArgs2; // The command line args to use when starting this exe while already running. 

Logger::Level logLevel = Logger::LevelError; // The log level for logging.
BOOL simulateNoMicphone = FALSE; // Simulate no microphone for testing.
std::wstring preferredUILanguages; // Comma separated list or empty.
bool waitForDebugger = false; // Show a message box when starting, giving a chance to attach a debugger.

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK HotKeySettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SoundSettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

std::unique_ptr<MicCtrl> micCtrl;
bool micMuted = false;
std::wstring configFilePath, startOnBootCmd;
bool silentMode = false;

HMENU popupMenu = NULL;
HMENU pluginMenu = NULL;
HMENU helpMenu = NULL;
HMENU startOnBootMenu = NULL;

// Command line args of microphone commands.
enum MIC_CMD {
    CMD_NONE,   // No command.
    CMD_ON,     // Turn on microphone.
    CMD_OFF,    // Turn off microphone.
    CMD_TOGGLE, // Toggle on/off.

    CMD_SCHED_TASK = 0xFF,  // Create a scheduler task to run this app on logon.
    CMD_RELAUNCH,           // This app is relaunching.

    CMD_SILENT = 0x8F000000, //Do not show notification.
};

DWORD ParseCmdLine(int argc, wchar_t** argv);
std::wstring CommandLine(const std::wstring& args);

std::wstring GetDefaultLogFilePath();
std::wstring defaultLogFilePath = GetDefaultLogFilePath();
static void SetPreferredUILanguages();
static int SchedTask(const std::wstring& action);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Read config file.
    configFilePath = moduleFilePath;
    const auto sep = configFilePath.rfind(L'\\');
    if (sep != std::wstring::npos) {
        configFilePath = configFilePath.substr(0, sep + 1) + CONFIG_FILE_NAME;
    }
    ReadConfig();

    if(waitForDebugger) {
        MessageBoxW(NULL, L"Waiting for debugger to attach. Click OK after attaching.", L"DeMic", MB_OK);
	}

    std::ofstream loggerStream(defaultLogFilePath, std::ios::app);
    Logger defaultLogger(&loggerStream, logLevel);
    SetDefaultLogger(&defaultLogger);
    LOG(Logger::LevelDebug, (std::wstringstream() << L"started.").str().c_str()); // Test logger

	instanceLock = std::make_unique<InstanceLock>();

    // Initialize COM with apartment model.
    VERIFY_SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    VERIFY_SUCCEEDED(CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, 0, nullptr))

    SetPreferredUILanguages();

    // Initialize global strings
    strRes = std::make_unique<StringRes>(hInstance);
    appTitle = strRes->Load(IDS_APP_TITLE);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLine(), &argc);
    DWORD cmd = ParseCmdLine(argc, argv);
    if (cmd == CMD_SCHED_TASK) {
        // /sched_task must be executed in an elevated process.
        if (!processElevated) {
            return -1;
        }
        if (argc < 3) {
            return -2;
        }
        return SchedTask(argv[2]);
    } else if (cmd == CMD_RELAUNCH) {
        // Relaunching, wait for the previous instance to exit.
		instanceLock->Acquire(INFINITE);
    }
    silentMode = cmd & CMD_SILENT;
    LocalFree(argv);


    micCtrl = std::make_unique<MicCtrl>(simulateNoMicphone);

    micCtrl->SetDevFilter(devFilter);
    
    const bool noArgInCmdLine = cmd == CMD_NONE;

    if (noArgInCmdLine) {
        // If no arg in command line, try to parse the args in ini file.
        int cmdLineArgc = 0;
        wchar_t** cmdLineArgv = CommandLineToArgvW(CommandLine(cmdLineArgs).c_str(), &cmdLineArgc);
        cmd = ParseCmdLine(cmdLineArgc, cmdLineArgv);
        LocalFree(cmdLineArgv);
        silentMode = cmd & CMD_SILENT;
    }


    if (!instanceLock->TryAcquire()) {
        if (noArgInCmdLine) {
            // If no arg in command line, try to parse the args in ini file.
            int cmdLine2Argc = 0;
            wchar_t** cmdLine2Argv = CommandLineToArgvW(CommandLine(cmdLineArgs2).c_str(), &cmdLine2Argc);
            cmd = ParseCmdLine(cmdLine2Argc, cmdLine2Argv);
            LocalFree(cmdLine2Argv);
        }
        const DWORD action = cmd & ~CMD_SILENT;
        if (action != 0) {
            // Find the main window of running process.
            const HWND hwnd = FindWindowW(szWindowClass, appTitle.c_str());
            if (hwnd != NULL) {
                PostMessage(hwnd, UM_MIC_CMD, action, 0);
                return 0;
            }
        }
        MessageBoxW(NULL, strRes->Load(IDS_ALREADY_RUNNING).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return 1;
    }


    startOnBootCmd = std::wstring(L"\"") + moduleFilePath + L"\" /silent";

    if (!MyRegisterClass(hInstance)) {
        LOG_LAST_ERROR();
        return FALSE;
    }

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DEMIC));
    MSG msg = {};

    // Process microphone comands from command line.
    const DWORD action = cmd & ~CMD_SILENT;
    if (action != CMD_NONE) {
        PostMessage(mainWindow, UM_MIC_CMD, action, 0);
    }


    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (OnPluginPreTranslateMessage(&msg)) {
            continue;
        }
        if (TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int) msg.wParam;
}

static void SetPreferredUILanguages() {
    if(preferredUILanguages.empty()) {
        return;
	}
    std::vector<wchar_t> buf;
    Split(preferredUILanguages, L",", [&buf](const auto& lang) {
        buf.insert(buf.end(), lang.begin(), lang.end());
        buf.push_back(L'\0');
    });
    buf.push_back(L'\0');
    if (!SetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, buf.data(), nullptr)) {
        LOG_LAST_ERROR();
    }
}

static int SchedTask(const std::wstring& action) {
    if (action == L"create") {
        return RegisterLogonTask(processSID, true) ? 0 : -4;
    } else if(action == L"delete") {
		return UnregisterLogonTask(processSID) ? 0 : -5;
    }
    return -3;
}

// CommandLine returns the whole command line of this exe
// as if args are passed in command line.
std::wstring CommandLine(const std::wstring& args) {
    return (std::wstringstream() << L'"' << moduleFilePath << L'"' << L' ' << args).str();
}

DWORD ParseCmdLine(int argc, wchar_t** argv) {
    DWORD  cmd = CMD_NONE;

    if (argc > 1) {
        const auto argv1 = std::wstring(argv[1]);
        if (argv1 == L"/silent" || argv1 == L"-silent") {
            cmd = CMD_SILENT;
        } else if (argv1 == L"/on" || argv1 == L"-on") {
            cmd = CMD_ON | CMD_SILENT;
        } else if (argv1 == L"/off" || argv1 == L"-off") {
            cmd = CMD_OFF | CMD_SILENT;
        } else if (argv1 == L"/toggle" || argv1 == L"-toggle") {
            silentMode = true;
            cmd = CMD_TOGGLE | CMD_SILENT;
        } else if (argv1 == L"/sched_task" || argv1 == L"-sched_task") {
			cmd = CMD_SCHED_TASK;
        } else if (argv1 == L"/relaunch" || argv1 == L"-relaunch") {
            cmd = CMD_RELAUNCH;
        }
    }
    return cmd;
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
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
   hInst = hInstance; // Store instance handle in our global variable

   mainWindow = CreateWindowW(szWindowClass, appTitle.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!mainWindow) {
      return FALSE;
   }

   // These menus are never destroyed in code.
   popupMenu = GetSubMenu(LoadMenuW(hInst, MAKEINTRESOURCEW(IDR_NOTIF_MENU)), 0);
   // We make the Help menu a separated menu resource because there is no
   // way to specify both a submenu and an ID for a menu item in menu resource.
   // Without an ID, we can not find the Help menu at runtime.
   helpMenu = GetSubMenu(LoadMenu(hInst, MAKEINTRESOURCEW(IDR_HELP_MENU)), 0);
   MENUITEMINFOW menuInfo = { sizeof(menuInfo), MIIM_SUBMENU, 0, 0, 0, helpMenu, 0 };
   if (!SetMenuItemInfoW(popupMenu, ID_MENU_HELP, FALSE, &menuInfo)) {
       LOG_LAST_ERROR();
   }

   startOnBootMenu = GetSubMenu(LoadMenu(hInst, MAKEINTRESOURCEW(IDR_START_ON_BOOT)), 0);
   menuInfo.hSubMenu = startOnBootMenu;
   if (!SetMenuItemInfoW(popupMenu, ID_MENU_START_ON_BOOT_ROOT, FALSE, &menuInfo)) {
       LOG_LAST_ERROR();
   }


   pluginMenu = CreatePopupMenu();
   menuInfo = { sizeof(menuInfo), MIIM_SUBMENU, 0, 0, 0, pluginMenu, 0 };
   if (!SetMenuItemInfoW(popupMenu, ID_MENU_PLUGIN, FALSE, &menuInfo)) {
       LOG_LAST_ERROR();
   }

   LoadPlugins();

   return TRUE;
}

static HHOOK HotkeySettingsHook = nullptr;

static LRESULT CALLBACK HotKeySettingsMsgFilterHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != 0) {
		return CallNextHookEx(HotkeySettingsHook, code, wParam, lParam);
    }
	MSG* msg = (MSG*)lParam;
    if(msg->message == WM_KEYDOWN && msg->wParam == VK_CANCEL) {
		// Intercept Ctrl+Break to prevent it from closing the hotkey settings dialog.
        TranslateMessage(msg);
        DispatchMessageW(msg);
        return 1;
	}
    return CallNextHookEx(HotkeySettingsHook, code, wParam, lParam);
}

void ShowHotKeySettingsWindow() {
    if (hotKeySettingWindow) {
		SetForegroundWindow(hotKeySettingWindow);
        return;
    }
    HotkeySettingsHook = SetWindowsHookExW(WH_MSGFILTER, HotKeySettingsMsgFilterHookProc, nullptr, GetCurrentThreadId());
    if (!HotkeySettingsHook) {
		LOG_LAST_ERROR();
    }
	DialogBoxW(hInst, MAKEINTRESOURCEW(IDD_HOTKEY_SETTINGS), mainWindow, HotKeySettings);
    if(HotkeySettingsHook) {
        UnhookWindowsHookEx(HotkeySettingsHook);
        HotkeySettingsHook = nullptr;
	}
}

void ShowSoundSettingsWindow() {
    if (soundSettingsWindow) {
		SetForegroundWindow(soundSettingsWindow);
        return;
    }
    DialogBoxW(hInst, MAKEINTRESOURCEW(IDD_SOUND_SETTINGS), mainWindow, SoundSettings);
}

// Opens the folder containing the specified file and select the file.
// If the filePath does not exist, returns FALSE.
static BOOL OpenFolderSelectFile(LPCWSTR filePath) {
    std::vector<wchar_t> buf(wcslen(filePath) + 1); // In case filePath contains . or ..
    DWORD len = GetFullPathNameW(filePath, DWORD(buf.size()), buf.data(), NULL);
    if (len == 0 || len > DWORD(buf.size())) {
		LOG_LAST_ERROR(); // If filePath contains . or .. the result length must be equal or less than buf.size().
        return FALSE;
    }
    BOOL ok = FALSE;

    PIDLIST_ABSOLUTE pidlFile = NULL;
    SFGAOF sfgao;

    auto hr = SHParseDisplayName(&buf[0], NULL, &pidlFile, 0, &sfgao);
    if (SUCCEEDED(hr) && pidlFile) {
        PIDLIST_ABSOLUTE pidlFolder = ILClone(pidlFile);
        if (pidlFolder && ILRemoveLastID(pidlFolder)) {
            PCUITEMID_CHILD child = ILFindLastID(pidlFile);
            if (SUCCEEDED(SHOpenFolderAndSelectItems(pidlFolder, 1, &child, 0))) {
                ok = TRUE;
            }
        }
        if (pidlFolder) CoTaskMemFree(pidlFolder);
        CoTaskMemFree(pidlFile);
    } else {
        LOG_ERROR(hr);
    }
    return ok;
}
// Open the specified folder.
static BOOL OpenFolder(LPCWSTR folder) {
    HINSTANCE h = ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)h > 32);
}

void ProcessNotifyMenuCmd(HWND hWnd, UINT_PTR cmd) {
    bool ok = false;
    switch (cmd) {
    case ID_MENU_HOTKEY_SETTINGS:
        ShowHotKeySettingsWindow();
        break;
    case ID_MENU_SOUND_SETTINGS:
        ShowSoundSettingsWindow();
        break;
    case ID_MENU_START_ON_BOOT:
        if (StartOnBootStatus() == LTS_REGISTERED) {
            ok = DisableStartOnBoot();
        } else {
            ok = EnableStartOnBoot(false);
        }
        if (!ok) {
            ShowError(strRes->Load(IDS_CHANGE_SETTING_FAILED).c_str());
        }
        break;
    case ID_MENU_START_ON_BOOT_AS_ADMIN:
        if (StartOnBootStatus() == LTS_REGISTERED_AS_ADMIN) {
            ok = DisableStartOnBoot();
        } else {
            ok = EnableStartOnBoot(true);
            if (ok && !processElevated) {
                if(MessageBoxW(mainWindow, strRes->Load(IDS_RUN_AS_ADMIN_OR_NOT).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONQUESTION | MB_YESNO) == IDYES) {
                    if (!RelaunchAsAdmin()) {
                        ShowError(strRes->Load(IDS_RELAUNCH_FAILED).c_str());
                    }
				}
            }
        }
        if (!ok) {
            ShowError(strRes->Load(IDS_CHANGE_SETTING_FAILED).c_str());
        }
		break;
    case ID_MENU_EXIT:
        SendMessage(mainWindow, WM_CLOSE, 0, 0);
        break;
    case ID_NO_PLUGIN:
        MessageBoxW(hWnd, (strRes->Load(IDS_PLUGIN_INSTRUCTION) + GetPluginDir()).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        break;
    case ID_HELP_SHOW_LOG:
        if (!OpenFolderSelectFile(defaultLogFilePath.c_str())) {
			OpenFolder(defaultLogFilePath.substr(0, defaultLogFilePath.rfind(LOG_FILE_NAME)).c_str());
        }
        break;
    case ID_HELP_CHECK_FOR_UPDATES:
        CheckForUpdate(hInst, mainWindow, UM_UPDATE_CHECK_DONE);
        break;
    case ID_HELP_RUN_AS_ADMIN:
        if (!RelaunchAsAdmin()) {
			ShowError(strRes->Load(IDS_RELAUNCH_FAILED).c_str());
        }
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

// NULL path to stop sound.
void PlaySoundFile(LPCWSTR path) {
    if (path) {
        DWORD dwAttrib = GetFileAttributes(path);
        if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            LOG_ERROR((std::wstringstream() << L"File path does not exist: " << path).str().c_str());
        }
    }
    // Async sond playing does not return FALSE if path does not exist.
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

static bool showingUnknownMicError = false; // Avoid showing multiple error message boxes when mic state is unknown.

void TurnOnMic() {
    const auto state = micCtrl->GetMuteState();
    if (state == MicCtrl::MuteState::Unknown) {
        if (showingUnknownMicError) {
            SetForegroundWindow(mainWindow);
            return;
        }
        showingUnknownMicError = true;
		ShowError(strRes->Load(IDS_NO_MICROPHONE).c_str());
        showingUnknownMicError = false;
        return;
    }
    if (state == MicCtrl::MuteState::Muted) {
        micCtrl->SetMuted(false);
        PlayOnSound();
    }
}

void TurnOffMic() {
    const auto state = micCtrl->GetMuteState();
    if (state == MicCtrl::MuteState::Unknown) {
        if (showingUnknownMicError) {
            SetForegroundWindow(mainWindow);
            return;
        }
        showingUnknownMicError = true;
        ShowError(strRes->Load(IDS_NO_MICROPHONE).c_str());
        showingUnknownMicError = false;
        return;
    }
    if (state == MicCtrl::MuteState::Unmuted) {
        micCtrl->SetMuted(true);
        PlayOffSound();
    }
}

void ToggleMuted() {
    const auto state = micCtrl->GetMuteState();
    if (state == MicCtrl::MuteState::Unknown) {
        if (showingUnknownMicError) {
            SetForegroundWindow(mainWindow);
            return;
        }
        showingUnknownMicError = true;
        ShowError(strRes->Load(IDS_NO_MICROPHONE).c_str());
        showingUnknownMicError = false;
        return;
    }
    if (state == MicCtrl::Muted) {
        micCtrl->SetMuted(false);
        PlayOnSound();
    } else {
        micCtrl->SetMuted(true);
        PlayOffSound();
    }
    
}

MicCtrl::MuteState GetMuteState() {
    return micCtrl->GetMuteState();
}

// Debounce interval for device state change events (ms).
static const UINT DEVICE_CHANGE_DELAY = 1000;

// static member of template class must be defined for each instantiation.
std::map<UINT_PTR, TimeDebouncer<>*> TimeDebouncer<>::sInstances;

static void lastErrorLogger() {
    LOG_LAST_ERROR();
};

static TimeDebouncer<> deviceStateChangedDebouncer(
    DEVICE_CHANGE_DELAY, [] {
        micCtrl->ReloadDevices();
        UpdateNotification(mainWindow);
        CallPluginMicStateListeners();
    }, 
lastErrorLogger);

static TimeDebouncer<> defaultDeviceChangedDebouncer(
    DEVICE_CHANGE_DELAY,
    CallPluginDefaultDevChangedListeners,
    lastErrorLogger);

// Debounce interval for device muted state change events (ms).
static const UINT MUTED_STATE_CHANGE_DELAY = 20;

static TimeDebouncer<> mutedStatedChangedDebouncer(
    MUTED_STATE_CHANGE_DELAY, [] {
        UpdateNotification(mainWindow);
        CallPluginMicStateListeners();
    },
lastErrorLogger);

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
        return 0;
    case WM_CREATE:
        if (!ResetHotKey(hWnd)) {
            // Clear hot key values if unable to register the hot key.
            hotKeyInfo.SetValue(0);
        }
        ShowNotification(hWnd, silentMode);
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            ToggleMuted();
        }
        break;
    case MicCtrl::WM_DEVICE_STATE_CHANGED:
		deviceStateChangedDebouncer.Emit();
        return 0;
    case MicCtrl::WM_DEFAULT_DEVICE_CHANGED:
        defaultDeviceChangedDebouncer.Emit();
        return 0;
    case MicCtrl::WM_MUTED_STATE_CHANGED:
		mutedStatedChangedDebouncer.Emit();
        return 0;
    case UM_NOTIFY:
        if (lParam == WM_RBUTTONUP) {
            SetForegroundWindow(hWnd);
            if (!IsWindowEnabled(mainWindow)) {
                // Has modal dialog open, do not show menu.
                return 0;
            }
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
    case UM_UPDATE_CHECK_DONE:
        OnUpdateCheckDone(hWnd, wParam, lParam);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code that uses hdc here...
        EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        CancelUpdateCheck();
        WriteConfig();
        RemoveNotification(hWnd);
        UnregisterHotKey(hWnd, HOTKEY_ID);
        PostQuitMessage(0);
        break;
    case WM_INITMENUPOPUP: {
            auto menu = (HMENU)wParam;
            if (menu == popupMenu) {
				CheckMenuItem(menu, ID_MENU_START_ON_BOOT_ROOT, MF_BYCOMMAND | (StartOnBootStatus() >= LTS_REGISTERED ? MF_CHECKED : MF_UNCHECKED));
                CallPluginInitMenuPopupListeners(NULL);
                break;
            }
            if (menu == pluginMenu) {
                OnPluginMenuInitPopup();
                break;
            }
            if (menu == helpMenu) {
                MENUITEMINFO info = { sizeof(info) };
                info.fMask = MIIM_STATE | MIIM_STRING;
                const bool checkingForUpdate = CheckingForUpdate();
                info.fState = checkingForUpdate ? MFS_DISABLED : MFS_ENABLED;
                info.dwTypeData = (LPWSTR)strRes->Load(checkingForUpdate ? IDS_CHECKING_FOR_UPDATES : IDS_CHECK_FOR_UPDATES).c_str();
                if (!SetMenuItemInfoW(menu, ID_HELP_CHECK_FOR_UPDATES, FALSE, &info)) {
                    LOG_LAST_ERROR();
                }
                info.fState = processElevated ? MFS_DISABLED : MFS_ENABLED;
                info.dwTypeData = (LPWSTR)strRes->Load(processElevated ? IDS_RUNNING_AS_ADMIN : IDS_RUN_AS_ADMIN).c_str();
                if (!SetMenuItemInfoW(menu, ID_HELP_RUN_AS_ADMIN, FALSE, &info)) {
                    LOG_LAST_ERROR();
                }
                break;
            }
            if (menu == startOnBootMenu) {
                CheckMenuItem(menu, ID_MENU_START_ON_BOOT, MF_BYCOMMAND | (StartOnBootStatus() == LTS_REGISTERED ? MF_CHECKED : MF_UNCHECKED));
				CheckMenuItem(menu, ID_MENU_START_ON_BOOT_AS_ADMIN, MF_BYCOMMAND | (StartOnBootStatus() == LTS_REGISTERED_AS_ADMIN ? MF_CHECKED : MF_UNCHECKED));
                break;
            }
            CallPluginInitMenuPopupListeners(menu);
            break;
        }
    }
   return DefWindowProc(hWnd, message, wParam, lParam);
}

// ResetHotKey resets the hot key.
bool ResetHotKey(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_ID);
    if (hotKeyInfo.Empty()) {
        return true;
    }
	if (!hotKeyInfo.RegisterHotKey(hwnd, HOTKEY_ID)) {
        DWORD lastError = GetLastError();
        if (lastError == 1409) { // 1409: ERROR_HOTKEY_ALREADY_REGISTERED
            ShowError(strRes->Load(IDS_HOTKEY_CONFILCT).c_str());
        } else {
            LOG_LAST_ERROR();
        }
        return false;
    }
    return true;
}

// Message handler for hotkey settings box.
INT_PTR CALLBACK HotKeySettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        hotKeyInfo.SetToCtrl(GetDlgItem(hDlg, IDC_HOTKEY));
        hotKeySettingWindow = hDlg;
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
                // Get the hot key setting of hot key control.
                HWND hotKeyCtrl = GetDlgItem(hDlg, IDC_HOTKEY);
				hotKeyInfo.ReadFromCtrl(hotKeyCtrl);
                // Reset the system hot key.
                if (!ResetHotKey(mainWindow)) {
                    // Clear hot key values if unable to register the hot key.
					hotKeyInfo.SetValue(0);
                    // Clear the hot key control.
                    SendMessage(hotKeyCtrl, HKM_SETHOTKEY, 0, 0);
                    SetFocus(hotKeyCtrl);
                    break;
                }
				UpdateNotification(mainWindow);
                WriteConfig();
            }
            // fallthrough
        case IDCANCEL:
            EndDialog(hDlg, 0);
            hotKeySettingWindow = NULL;
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
            LOG_ERROR(err);
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
        Button_SetCheck(GetDlgItem(hDlg, IDC_ENABLE_ON_SOUND), enableOnSound);
        SetWindowTextW(GetDlgItem(hDlg, IDC_ON_SOUND_PATH), onSoundPath.empty() ? strRes->Load(IDS_NAN).c_str() : onSoundPath.c_str());
        SetWindowTextW(GetDlgItem(hDlg, IDC_OFF_SOUND_PATH), offSoundPath.empty() ? strRes->Load(IDS_NAN).c_str() : offSoundPath.c_str());
        Button_SetCheck(GetDlgItem(hDlg, IDC_ENABLE_OFF_SOUND), enableOffSound);
        soundSettingsWindow = hDlg;
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
        case IDC_OFF_SOUND_PLAY: {
            buf[0] = 0;
            GetWindowTextW(GetDlgItem(hDlg, IDC_OFF_SOUND_PATH), buf, sizeof(buf) / sizeof(buf[0]));
            PlayOffSound(std::wstring(strRes->Load(IDS_NAN) == buf ? L"" : buf));
            break;
        }
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
            // fallthorugh
        }
        case IDCANCEL:
            EndDialog(hDlg, 0);
			soundSettingsWindow = NULL;
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// ID of Shell_NotifyIconW.
static const UINT NOTIFY_ID = 1;

struct ShellNotifyIconRetryData {
    DWORD message;
    NOTIFYICONDATAW data;
	DWORD retriedCount;
};

// static member of template class must be defined for each instantiation.
std::map<UINT_PTR, TimeDebouncer<ShellNotifyIconRetryData>*> TimeDebouncer<ShellNotifyIconRetryData>::sInstances;


// Retry interval for Shell_NotifyIconW(NIM_MODIFY or NIM_ADD).
static const UINT SHELL_NOTIFY_ICON_RETRY_INTERVAL = 1000;

static TimeDebouncer<ShellNotifyIconRetryData>shellNotifyIconRetryDebouncer(
    SHELL_NOTIFY_ICON_RETRY_INTERVAL, 
    [](auto data) {
	    LOG(Logger::LevelDebug,
            (std::wstringstream() << L"Retrying Shell_NotifyIconW " << data.retriedCount+1 << L" ...").str().c_str());
        if (Shell_NotifyIconW(data.message, &data.data)) {
            LOG(Logger::LevelDebug, L"Shell_NotifyIconW succeeded on retry.");
            return;
        }
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT) {
		    if (++data.retriedCount < 3) { // Retry up to 3 times.
                shellNotifyIconRetryDebouncer.Emit(data);
                return;
            }
        } else {
            LOG_ERROR(err);
        }
    },
    lastErrorLogger);

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
    const auto state = micCtrl->GetMuteState();
    data.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(
        state == MicCtrl::Muted ? IDI_MICROPHONE_MUTED : 
        state == MicCtrl::Unmuted ? IDI_MICROPHONE : IDI_NO_MICROPHONE));
    std::wstring tipFmt = strRes->Load(IDS_NOTIFICATION_TIP);
    if (hotKeyInfo.Empty()) {
        wnsprintfW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], 
            strRes->Load(IDS_NOTIFICATION_TIP).c_str(), 
            VERSION);
    } else {
        wnsprintfW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], 
            strRes->Load(IDS_NOTIFICATION_TIP_HOTKEY).c_str(), 
            VERSION, hotKeyInfo.GetStr().c_str());
    }
    
	const DWORD message = modify ? NIM_MODIFY : NIM_ADD;
    if (!Shell_NotifyIconW(message, &data)) {
        DWORD err = GetLastError();
        if (err != ERROR_TIMEOUT) {
            LOG_ERROR(err);
            return;
        }
		LOG(Logger::LevelDebug, L"Shell_NotifyIconW failed with ERROR_TIMEOUT, will retry.");
		// Shell_NotifyIconW may fail with ERROR_TIMEOUT if Explorer is busy.
		// Prepare for retrying.
        shellNotifyIconRetryDebouncer.Emit(ShellNotifyIconRetryData{message, data, 0});
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
        LOG_LAST_ERROR();
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

// Command line args to use when starting the first
// instance of this exe.
static const auto CONFIG_CMD_LINE_ARGS = L"CmdLineArgs";
// Command line args to use when executing this exe
// while another instance is already running.
static const auto CONFIG_CMD_LINE_ARGS2 = L"CmdLineArgs2";
// Log level.
static const auto CONFIG_LOG = L"Log";
static const auto CONFIG_LOG_LEVEL = L"LogLevel";
static const auto CONFIG_DEBUG = L"Debug";
static const auto CONFIG_SIMULATE_NO_MICROPHONE = L"SimulateNoMicrophone";
static const auto CONFIG_PREFERRED_UI_LANGUAGES = L"PreferredUILanguages";
static const auto CONFIG_WAIT_FOR_DEBUGGER = L"WaitForDebugger";

// Read settings from config file.
void ReadConfig() {
    if (!PathFileExistsW(configFilePath.c_str())) {
        return;
    }
    const int value = GetPrivateProfileIntW(CONFIG_HOTKEY, CONFIG_VALUE, 0, configFilePath.c_str());
    hotKeyInfo.SetValue((WORD)value);

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

    buf[0] = 0;
    GetPrivateProfileStringW(CONFIG_CMD_LINE_ARGS, CONFIG_CMD_LINE_ARGS, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
    cmdLineArgs = buf;

    buf[0] = 0;
    GetPrivateProfileStringW(CONFIG_CMD_LINE_ARGS, CONFIG_CMD_LINE_ARGS2, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
    cmdLineArgs2 = buf;

    logLevel = (Logger::Level)GetPrivateProfileIntW(CONFIG_LOG, CONFIG_LOG_LEVEL, Logger::LevelError, configFilePath.c_str());

	simulateNoMicphone =  GetPrivateProfileIntW(CONFIG_DEBUG, CONFIG_SIMULATE_NO_MICROPHONE, 0, configFilePath.c_str()) == 1;

	buf[0] = 0;
	GetPrivateProfileStringW(CONFIG_DEBUG, CONFIG_PREFERRED_UI_LANGUAGES, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
	preferredUILanguages = buf;
    
    waitForDebugger = GetPrivateProfileIntW(CONFIG_DEBUG, CONFIG_WAIT_FOR_DEBUGGER, 0, configFilePath.c_str()) != 0;

}

// Write settings to config file.
void WriteConfig() {
    WritePrivateProfileStringW(CONFIG_HOTKEY, CONFIG_VALUE,
        std::to_wstring(hotKeyInfo.GetValue()).c_str(),
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
    WritePrivateProfileStringW(CONFIG_CMD_LINE_ARGS, CONFIG_CMD_LINE_ARGS,
        cmdLineArgs.c_str(),
        configFilePath.c_str());
    WritePrivateProfileStringW(CONFIG_CMD_LINE_ARGS, CONFIG_CMD_LINE_ARGS2,
        cmdLineArgs2.c_str(),
        configFilePath.c_str());
}

static const auto START_ON_BOOT_REG_SUB_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const auto START_ON_BOOT_REG_VALUE_NAME = L"DeMic";

bool StartOnBootEnabled_Reg() {
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

void EnableStartOnBoot_Reg() {
    HKEY key = NULL;
    auto ret = RegOpenKeyExW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY,
        0, KEY_WRITE,
        &key);
    if (ret != ERROR_SUCCESS) {
        LOG_ERROR(ret);
    }
    else {
        ret = RegSetValueExW(key, START_ON_BOOT_REG_VALUE_NAME,
            0, REG_SZ,
            (const BYTE*)startOnBootCmd.c_str(), DWORD(startOnBootCmd.length()) * sizeof(wchar_t));
        if (ret != ERROR_SUCCESS) {
            LOG_ERROR(ret);
        }
    }
    RegFlushKey(key);
    RegCloseKey(key);
}

void DisableStartOnBoot_Reg() {
    HKEY key = NULL;
    auto ret = RegOpenKeyExW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY,
        0, KEY_WRITE,
        &key);
    if (ret != ERROR_SUCCESS) {
        LOG_ERROR(ret);
    }
    else {
        ret = RegDeleteValueW(key, START_ON_BOOT_REG_VALUE_NAME);
        if (ret != ERROR_SUCCESS) {
            LOG_ERROR(ret);
        }
    }
    RegFlushKey(key);
    RegCloseKey(key);
}

LogonTaskStatus StartOnBootStatus() {
    return GetLogonTaskStatus(processSID);
}

bool EnableStartOnBoot(bool asAdmin) {
    if (!asAdmin) {
        if (RegisterLogonTask(processSID, false)) {
            return true;
        }
        // Maybe registered with different privileges, try to unregister first.
        // DisableStartOnBoot will handle ULA and non-ULA accounts correctly.
        if (!DisableStartOnBoot()) {
            return false;
        }
        return RegisterLogonTask(processSID, false);
    }
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";                // Trigger UAC
    sei.lpFile = moduleFilePath.c_str();
    sei.lpParameters = L" /sched_task create";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        if (!CloseHandle(sei.hProcess)) {
            LOG_LAST_ERROR();
            return false;
        }
        return true;
    }
    return false;
}

bool DisableStartOnBoot() {
    const auto status = StartOnBootStatus();
    if (status == LTS_UNREGISTERED) {
         return true;
	}
    if (StartOnBootStatus() == LTS_REGISTERED) {
        if (UnregisterLogonTask(processSID)) {
			return true;
        }
		// Maybe the task is registered with admin privileges, try to unregister with admin privileges.
    }
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";                // Trigger UAC
    sei.lpFile = moduleFilePath.c_str();
    sei.lpParameters = L" /sched_task delete";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        if (!CloseHandle(sei.hProcess)) {
            LOG_LAST_ERROR();
			return false;
        }
        return true;
    }
    return false;
}

bool RelaunchAsAdmin() {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";                // Trigger UAC
    sei.lpFile = moduleFilePath.c_str();
    sei.lpParameters = L" /relaunch";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        // Exit ASAP.
        SendMessage(mainWindow, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}

static const auto RUNNING_MUTEX_NAME = L"DeMic is running";
static bool AlreadyRunning() {
    if (CreateMutexW(NULL, FALSE, RUNNING_MUTEX_NAME) == NULL) {
        LOG_LAST_ERROR();
        std::exit(1);
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
static bool IsAnotherInstanceRunning() {
    HANDLE hMutex = CreateMutexW(NULL, FALSE, RUNNING_MUTEX_NAME);
    if (hMutex == NULL) {
        LOG_LAST_ERROR();
        std::exit(1);
    }
    bool ret = GetLastError() == ERROR_ALREADY_EXISTS;
    CloseHandle(hMutex);
    return ret;
}

std::wstring GetDefaultLogFilePath() {
    std::wstring logFilePath(moduleFilePath);
    const auto sep = logFilePath.find_last_of(L'\\');
    if (sep != std::wstring::npos) {
        logFilePath = logFilePath.substr(0, sep + 1) + LOG_FILE_NAME;
    }
    return logFilePath;
}
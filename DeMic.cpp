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
#include <filesystem>
#include <thread>
#include <exception>

#include "Plugin.h"
#include "TimeDebouncer.h"
#include "Log.h"
#include "AutoUpdateMessageBox.h"

#include "UpdateChecker.h"
#include "HotKeyControlInfo.h"
#include "TaskScheduler.h"
#include "InstanceLock.h"
#include "JSONState.h"
#include "UiFont.h"


// IDs of Shell_NotifyIconW.
enum {
    MAIN_NOTIFICATION_ID = 1,
    MANUAL_UPDATE_NOTIFICATION_ID,
    AUTO_UPDATE_NOTIFICATION_ID,
};

static const auto STATE_AUTO_UPDATE = "AutoUpdate";
static const auto STATE_AUTO_UPDATE_IGNORE = "IgnoreVersion";
static const auto STATE_AUTO_UPDATE_LAST_SUCCESS_TIME = "LastSuccessTime";

JSONState* appState = NULL;

std::unique_ptr<InstanceLock> instanceLock;

enum {
    // Notify messages used by Shell_NotifyIconW.
    // Shell_NotifyIconW use 0 id for all NIN_BALLOON* messages, so
    // these notification icons can't share the same notify message.
    UM_MAIN_NOTIFICATION_NOTIFY = WM_USER + 1,
    UM_MANUAL_UPDATE_NOTIFICATION_NOTIFY,
    UM_AUTO_UPDATE_NOTIFICATION_NOTIFY,

    // Microphone command message used by command line args.
    UM_MIC_CMD,
    // Done message used by UpdateChecker.
    UM_MANUAL_UPDATE_CHECK_DONE,
    // Done message used by UpdateChecker for automatic checking.
    UM_AUTO_UPDATE_CHECK_DONE,
    // Posted by the watcher of the system colour mode registry value.
    UM_SYSTEM_COLOR_MODE_CHANGED,
};
// Message used by Shell to notify the app that the taskbar is created.
// Registered with RegisterWindowMessageW("TaskbarCreated").
static UINT wmTaskbarCreated = 0;

static UINT micIconResID = IDI_MICROPHONE;
static UINT micMutedIconResID = IDI_MICROPHONE_MUTED;
static UINT updateIconResID = IDI_UPDATE_AVAILABLE;
static UINT noMicIconResID = IDI_NO_MICROPHONE;
// The colour mode the icon resource IDs above were chosen for.
static bool appliedDarkMode = true;
static const int HOTKEY_ID = 1;

// The updated version information retrieved by update checker.
struct AvailableUpdate {
    AvailableUpdate(const WSemVer& version, const std::wstring& url) :Version(version), Url(url) {}
    AvailableUpdate(const AvailableUpdate& r):Version(r.Version), Url(r.Url) {}
    const WSemVer Version;
    const std::wstring Url;
};
static std::optional<AvailableUpdate> availableManualUpdate, availableAutoUpdate;

static const wchar_t* const CONFIG_FILE_NAME = L"DeMic.ini";
const static wchar_t* const LOG_FILE_NAME = L"Log.txt";
// Full path of this exe.
const std::wstring moduleFilePath = GetModuleFilePath(); 
// User SID of current process.
const std::wstring processSID = GetCurrentUserSid();
// Whether the current process is elevated.
const bool processElevated = IsCurrentProcessElevated();

std::unique_ptr <StringRes> strRes;

static UINT MainNotificationIconResID();
static void ShowNotification(UINT id, std::function<void(NOTIFYICONDATAW&)> onPrepareData);
static void RemoveNotification(UINT id);
static void ShowMainNotificationImpl(const std::wstring& info);
static void ShowMainNotification(bool silent);
static void UpdateMainNotification();
static void RemoveMainNotification();
static void ShowManualUpdateNotification(bool silent);
static void ShowNoUpdateNotification();
static void RemoveNoUpdateNotification();
static void RemoveManualUpdateNotification();
static void ShowAutoUpdateNotification(bool silent);
static void RemoveAutoUpdateNotification();
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
BOOL simulateNoMicrophone = FALSE; // Simulate no microphone for testing.
std::wstring preferredUILanguages; // Comma separated list or empty.
bool waitForDebugger = false; // Show a message box when starting, giving a chance to attach a debugger.
std::wstring simulateVersionForUpdateCheck; // Simulate a version for update check, for testing.
bool autoCheckForUpdates = false; // Automatically check for updates on startup.

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
HMENU updateMenu = NULL;

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

// Whether the taskbar uses the dark theme. Default to true if system setting is missing.
static bool IsSystemDarkMode() {
    // SystemUsesLightTheme is 1 for the light theme and 0 for the dark one.
    DWORD lightTheme = 0;
    DWORD size = sizeof(lightTheme);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(&lightTheme), &size) != ERROR_SUCCESS) {
            lightTheme = 0;
        }
        RegCloseKey(hKey);
    }
    return lightTheme == 0;
}

static void ChangeIconResIDs(bool darkMode) {
    if (darkMode) {
        micIconResID = IDI_MICROPHONE;
        micMutedIconResID = IDI_MICROPHONE_MUTED;
        updateIconResID = IDI_UPDATE_AVAILABLE;
        noMicIconResID = IDI_NO_MICROPHONE;
    } else {
        micIconResID = IDI_MICROPHONE_BLACK;
        micMutedIconResID = IDI_MICROPHONE_MUTED_BLACK;
        updateIconResID = IDI_UPDATE_AVAILABLE_BLACK;
        noMicIconResID = IDI_NO_MICROPHONE_BLACK;
    }
    appliedDarkMode = darkMode;
}


// Retries a function until it returns true or GetLastError() is not ERROR_TIMEOUT.
// The return value is 0 if the function returns true, or the last error code otherwise.
template<typename F>
DWORD RetryWhileTimeout(const F& f, const wchar_t* op) {
    // A infinite loop is used here to wait for a busy system to become responsive.
    // i.e. If the shell is busy Shell_NotifyIconW() returns ERROR_TIMEOUT, my app
    // hangs. A laggy shell results in a laggy application, fair enough.
    // Further more, the shell can't always timeout in a living OS.
    DWORD delay = 100; // 100ms
    for(int i = 0; i < 10; i++) {
        SetLastError(0);
        if (f()) {
            return 0;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_TIMEOUT) {
            return error;
        }
        Sleep(delay);
        delay = delay * 3 / 2; // 1.5x
    }
    LOG(Logger::LevelError, std::format(L"Operation timed out: {}", op).c_str());
    ShowMessageBox(strRes->Load(IDS_TIMEOUT_ERROR).c_str(), appTitle.c_str(), MB_ICONERROR);
	TerminateProcess(GetCurrentProcess(), 1);
    return 0; // Make compiler happy.
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    ChangeIconResIDs(IsSystemDarkMode());

    // Read config file.
    configFilePath = moduleFilePath;
    const auto sep = configFilePath.rfind(L'\\');
    if (sep != std::wstring::npos) {
        configFilePath = configFilePath.substr(0, sep + 1) + CONFIG_FILE_NAME;
    }
    ReadConfig();

    if(waitForDebugger) {
        ShowMessageBox(L"Waiting for debugger to attach. Click OK after attaching.", L"DeMic", MB_OK);
	}

    // The logger must outlive all static storage objects, because their destructors may log
    // (e.g. ~MicCtrl via VERIFY_*). There is no way to know when the last log happens, 
    // so the logger and its stream are intentionally leaked; the process reclaims them at exit.
    SetDefaultLogger(
        // never deleted
        new Logger(
            // never deleted
            new std::ofstream(OpenRotatingLogFile(defaultLogFilePath, 100 * 1024, 1)),  // 100KB per file, 1 backup
            logLevel
        )
    );
    LOG(Logger::LevelDebug, (std::wstringstream() << L"started.").str().c_str()); // Test logger

    const std::wstring stateFilePath = std::filesystem::path(moduleFilePath).parent_path() / L"state.json";
    JSONState state(stateFilePath);
    appState = &state;

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


    micCtrl = std::make_unique<MicCtrl>(simulateNoMicrophone);

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
        ShowMessageBox(strRes->Load(IDS_ALREADY_RUNNING).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
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

    // Destroy the microphone controller before uninitializing COM. Its destructor calls
    // IMMDeviceEnumerator, which is dangling once COM is uninitialized (access violation on exit).
    micCtrl.reset();

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
    WNDCLASSEXW wcex = {};

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

   if (!CreateWindowW(szWindowClass, appTitle.c_str(), WS_OVERLAPPEDWINDOW,
       CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr)) {
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

   updateMenu = GetSubMenu(LoadMenu(hInst, MAKEINTRESOURCEW(IDR_UPDATE_MENU)), 0);
   menuInfo = { sizeof(menuInfo), MIIM_SUBMENU, 0, 0, 0, updateMenu, 0 };
   if (!SetMenuItemInfoW(popupMenu, ID_HELP_UPDATE, FALSE, &menuInfo)) {
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

static void ShowHotKeySettingsWindow() {
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

static void ShowSoundSettingsWindow() {
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

static void ProcessNotifyMenuCmd(HWND hWnd, UINT_PTR cmd) {
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
                if(ShowMessageBox(strRes->Load(IDS_RUN_AS_ADMIN_OR_NOT).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONQUESTION | MB_YESNO) == IDYES) {
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
        ShowMessageBox((strRes->Load(IDS_PLUGIN_INSTRUCTION) + GetPluginDir()).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        break;
    case ID_HELP_SHOW_LOG:
        if (!OpenFolderSelectFile(defaultLogFilePath.c_str())) {
			OpenFolder(defaultLogFilePath.substr(0, defaultLogFilePath.rfind(LOG_FILE_NAME)).c_str());
        }
        break;
    case ID_MENU_CHECK_FOR_UPDATES:
        CheckForUpdate(mainWindow, UM_MANUAL_UPDATE_CHECK_DONE);
        break;
    case ID_MENU_AUTO_CHECK_FOR_UPDATES:
        autoCheckForUpdates = !autoCheckForUpdates;
        WriteConfig();
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
static void PlaySoundFile(LPCWSTR path) {
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

static void PlaySystemSound(DWORD sndID) {
    PlaySound((LPCTSTR)(size_t)sndID, NULL, SND_ALIAS_ID | SND_NODEFAULT | SND_ASYNC | SND_SENTRY | SND_SYSTEM);
}

static void PlayOnSound() {
    if (enableOnSound) {
        PlayOnSound(onSoundPath);
    }
}

static void PlayOffSound() {
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


static void lastErrorLogger() {
    LOG_LAST_ERROR();
};

static const UINT DEVICE_CHANGE_DELAY = 1000; // 1s
static TimeDebouncer<> deviceStateChangedDebouncer(
    DEVICE_CHANGE_DELAY, [] {
        micCtrl->ReloadDevices();
        UpdateMainNotification();
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
        UpdateMainNotification();
        CallPluginMicStateListeners();
    },
lastErrorLogger);

static void prepareTaskbarCreatedNotification() {
    // https://learn.microsoft.com/en-us/windows/win32/shell/taskbar#taskbar-creation-notification
    wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if(!wmTaskbarCreated) {
        LOG_LAST_ERROR();
       return;
	}
    // Passthrough UIPI:
	// Allow the message to be sent to this window, even if the process is elevated.
    if (!ChangeWindowMessageFilterEx(mainWindow, wmTaskbarCreated, MSGFLT_ALLOW, NULL)) {
        LOG_LAST_ERROR();
        return;
    }
}

static BOOL NavigateURL(const wchar_t* url) {
    HINSTANCE h = ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)h > 32);
}

static std::chrono::hours HoursSinceEpoch(const std::chrono::system_clock::time_point& t) {
    return std::chrono::duration_cast<std::chrono::hours>(t.time_since_epoch());
}

static std::chrono::system_clock::time_point TimePointSinceEpoch(const std::chrono::hours h) {
    return std::chrono::system_clock::time_point(h);
}

constexpr UINT AUTO_UPDATE_DELAY = 30 * 1000; // 30s.
constexpr std::chrono::hours MIN_AUTO_UPDATE_INTERVAL = std::chrono::hours(12); // 12h

static void PerformAutoUpdateChecking() {
    if (!autoCheckForUpdates) {
        return;
    }
    // Time throttling.
    // Only applies to auto-updates themselves and is not affected by manual updates.
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto& state = appState->GetState();
    const auto autoUpdate = state.find(STATE_AUTO_UPDATE);
    if (autoUpdate != state.end() && autoUpdate->is_object()) {
        const auto lastSuccessTime = autoUpdate->find(STATE_AUTO_UPDATE_LAST_SUCCESS_TIME);
        if (lastSuccessTime != autoUpdate->end() && lastSuccessTime->is_number_integer() && lastSuccessTime->is_number_unsigned()) {
            const uint64_t h = *lastSuccessTime;
            constexpr uint64_t MAX_HOURS_SINCE_EPOCH = sys_time<hours>(sys_days(year_month_day{ year(3000), January, day(1) })).time_since_epoch().count(); // January 1st, 3000
            if (h > 0 && h < MAX_HOURS_SINCE_EPOCH) {
                const auto interval = now - TimePointSinceEpoch(hours(h));
                if (interval >= hours(0) && interval < MIN_AUTO_UPDATE_INTERVAL) {
                    LOG(Logger::LevelDebug,
                        std::format(L"Auto update checking canceled due to time throttling. Interval={:.2f}h Min={}h",
                            duration<float, hours::period>(interval).count(),
                            MIN_AUTO_UPDATE_INTERVAL.count()
                        ).c_str());
                    return; // Too frequent.
                }
            }
        }
    }
    LOG(Logger::LevelDebug, L"Check for updates automatically");
    CheckForUpdate(mainWindow, UM_AUTO_UPDATE_CHECK_DONE);
}
static TimeDebouncer<> autoUpdateStartupDebouncer(AUTO_UPDATE_DELAY, PerformAutoUpdateChecking, lastErrorLogger);

static void ProcessMainNotificationEvent(HWND hwnd, WORD event) {
    switch (event) {
    case WM_CONTEXTMENU: {
        SetForegroundWindow(hwnd);
        if (!IsWindowEnabled(hwnd)) {
            // Has modal dialog open, do not show menu.
            return;
        }
        POINT pt = { 0 };
        GetCursorPos(&pt);
        UINT_PTR cmd = TrackPopupMenu(popupMenu,
            TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
            pt.x, pt.y,
            0,
            hwnd, NULL);
        PostMessage(hwnd, WM_NULL, 0, 0);
        if (cmd != 0) {
            ProcessNotifyMenuCmd(hwnd, cmd);
        }
        break;
    }
    case NIN_SELECT:
        [[fallthrough]];
    case NIN_KEYSELECT:
        SetForegroundWindow(hwnd);
        ToggleMuted();
        break;
    }
}

void ProcessManualUpdateNotificationEvent(HWND hwnd, WORD event) {
    switch (event) {
    case WM_CONTEXTMENU:
        [[fallthrough]];
    case NIN_SELECT:
        [[fallthrough]];
    case NIN_KEYSELECT:
        [[fallthrough]];
    case NIN_BALLOONUSERCLICK:
        if (!IsWindowEnabled(mainWindow)) {
            // In modal loop, do not show messagebox and do not remove notification icon
            // so that the user can interact with the notification icon later.
            break; 
        }
        RemoveManualUpdateNotification();
        if (availableManualUpdate) {
            // New version available.
            auto message = strRes->Load(IDS_UPDATE_AVAILABLE) + availableManualUpdate->Version.str() + strRes->Load(IDS_UPDATE_OR_NOT);
            if (ShowMessageBox(message.c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONQUESTION | MB_YESNO) == IDYES) {
                if (!NavigateURL(availableManualUpdate->Url.c_str())) {
                    LOG_LAST_ERROR();
                }
            }
        }
        break;
    }
}

void ProcessAutoUpdateNotificationEvent(HWND hwnd, WORD event) {
    switch (event) {
    case WM_CONTEXTMENU:
        [[fallthrough]];
    case NIN_SELECT:
        [[fallthrough]];
    case NIN_KEYSELECT:
        [[fallthrough]];
    case NIN_BALLOONUSERCLICK:
        if (!IsWindowEnabled(mainWindow)) {
            // In modal loop, do not show messagebox and do not remove notification icon
            // so that the user can interact with the notification icon later.
            break;
        }
        RemoveAutoUpdateNotification();
        if (availableAutoUpdate) {
            // New version available.
            const auto version = availableAutoUpdate->Version.str();
            switch (AutoUpdateMessageBox(version)) {
            case IDYES:
                if (!NavigateURL(availableAutoUpdate->Url.c_str())) {
                    LOG_LAST_ERROR();
                }
                break;
			case IDNO: { // No and Cancel button are swapped positions to make No the default button. So IDNO means "Ignore this version".
                // Save ignored version.
                try {
                    auto& state = appState->GetState();
                    auto& autoUpdate = state[STATE_AUTO_UPDATE];
                    if (!autoUpdate.is_object()) {
                        autoUpdate = json::object();
                    }
                    const auto u8ver = ToUTF8(version);
                    const std::string ver(reinterpret_cast<const char*>(u8ver.data()), u8ver.length());
                    autoUpdate[STATE_AUTO_UPDATE_IGNORE] = ver;
                    appState->Commit();
                } catch (...) {
                    LOG(Logger::LevelError, L"Failed to write ignored version");
                }
                break;
            }
            }
        }
        break;
    }
}

// Reapplies the icon of a notification that is currently shown, e.g. after the
// system switched between the light and the dark theme.
static void UpdateNotificationIcon(UINT id, UINT iconResID, const wchar_t* name) {
    NOTIFYICONDATAW data = { 0 };
    data.cbSize = sizeof data;
    data.hWnd = mainWindow;
    data.uID = id;
    data.uFlags = NIF_ICON;
    data.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(iconResID));
    const auto err = RetryWhileTimeout([&data] {
        return Shell_NotifyIconW(NIM_MODIFY, &data);
	}, L"NIM_MODIFY");
    if (err == 0) {
        LOG(Logger::LevelInfo, std::format(L"Updated {} notification icon", name).c_str());
    }
}

// Reapplies the icon resources and the icons of the shown notifications when
// the system switched between the light and the dark theme.
static void ApplySystemColorMode() {
    const bool darkMode = IsSystemDarkMode();
    if (darkMode == appliedDarkMode) {
        return;
    }
    LOG(Logger::LevelInfo, L"System color mode changed");
    ChangeIconResIDs(darkMode);
    UpdateNotificationIcon(MAIN_NOTIFICATION_ID, MainNotificationIconResID(), L"main");
    UpdateNotificationIcon(MANUAL_UPDATE_NOTIFICATION_ID, updateIconResID, L"manual update");
    UpdateNotificationIcon(AUTO_UPDATE_NOTIFICATION_ID, updateIconResID, L"auto update");
}

static TimeDebouncer<> applySystemColorModeDebouncer(500, ApplySystemColorMode, lastErrorLogger);

// The colour mode is decided by this registry value, so watching the value
// itself is the primary way to notice colour mode changes.
// WM_SETTINGCHANGE is used as a fallback.
static constexpr wchar_t PERSONALIZE_REG_KEY[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

static std::thread* themeWatchThread = nullptr;
static HANDLE themeWatchStopEvent = nullptr;

// Must be called before the window is destroyed, because the watcher posts
// messages to it.
static void StopThemeWatcher() {
    if (themeWatchThread) {
        SetEvent(themeWatchStopEvent);
        themeWatchThread->join();
        delete themeWatchThread;
        themeWatchThread = nullptr;
    }
    if (themeWatchStopEvent) {
        CloseHandle(themeWatchStopEvent);
        themeWatchStopEvent = nullptr;
    }
}

// Watches PERSONALIZE_REG_KEY and posts changedMessage on every change.
static void StartThemeWatcher(HWND hwnd, UINT changedMessage) {
    StopThemeWatcher();
    HANDLE stopEvent = themeWatchStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent) {
        LOG_LAST_ERROR();
        return;
    }
    themeWatchThread = new std::thread([hwnd, changedMessage, stopEvent] {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, PERSONALIZE_REG_KEY, 0, KEY_NOTIFY, &key)
            != ERROR_SUCCESS) {
            LOG_LAST_ERROR();
            return;
        }
        HANDLE changeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!changeEvent) {
            LOG_LAST_ERROR();
            RegCloseKey(key);
            return;
        }
        const HANDLE handles[] = { stopEvent, changeEvent };
        for (;;) {
            // Asynchronous: arm and return, changeEvent is signalled on a change.
            if (RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_CHANGE_LAST_SET,
                changeEvent, TRUE) != ERROR_SUCCESS) {
                LOG_LAST_ERROR();
                break;
            }
            if (WaitForMultipleObjects(_countof(handles), handles, FALSE, INFINITE)
                != WAIT_OBJECT_0 + 1) {
                break; // Stopped.
            }
            PostMessageW(hwnd, changedMessage, 0, 0);
        }
        CloseHandle(changeEvent);
        RegCloseKey(key);
    });
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
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
        mainWindow = hWnd;
        prepareTaskbarCreatedNotification();
        StartThemeWatcher(hWnd, UM_SYSTEM_COLOR_MODE_CHANGED);
        // Catch a colour mode change that happened between reading the mode and
        // arming the watcher.
        ApplySystemColorMode();
        if (processElevated) {
			// Allow UM_MIC_CMD message to be sent from non-elevated process.
            if (!ChangeWindowMessageFilterEx(hWnd, UM_MIC_CMD, MSGFLT_ALLOW, NULL)) {
                LOG_LAST_ERROR();
            }
        }
        if (!ResetHotKey(hWnd)) {
            // Clear hot key values if unable to register the hot key.
            hotKeyInfo.SetValue(0);
        }
        LOG(Logger::Level::LevelDebug, L"Show notification from WM_CREATE");
        ShowMainNotification(silentMode);

        // Check for updates on startup.
        if(autoCheckForUpdates) {
            LOG(Logger::LevelDebug, L"Schedule auto update checking on startup");
            if (!simulateVersionForUpdateCheck.empty()) {
                PerformAutoUpdateChecking(); // No delay when debugging update checking.
            } else {
                autoUpdateStartupDebouncer.Emit();
            }
        }
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
    case UM_MAIN_NOTIFICATION_NOTIFY:
        // Note that NOTIFYICON_VERSION_4 is used.
        ProcessMainNotificationEvent(hWnd, LOWORD(lParam));
        return 0;
    case UM_MANUAL_UPDATE_NOTIFICATION_NOTIFY:
        // Note that NOTIFYICON_VERSION_4 is used.
        ProcessManualUpdateNotificationEvent(hWnd, LOWORD(lParam));
        return 0;
    case UM_AUTO_UPDATE_NOTIFICATION_NOTIFY:
        // Note that NOTIFYICON_VERSION_4 is used.
        ProcessAutoUpdateNotificationEvent(hWnd, LOWORD(lParam));
        return 0;
    case UM_SYSTEM_COLOR_MODE_CHANGED:
        applySystemColorModeDebouncer.Emit();
        return 0;
    case UM_MANUAL_UPDATE_CHECK_DONE:
        OnUpdateCheckDone(wParam, lParam,
            [](const WSemVer& current, const WSemVer& remote, const std::wstring& url) {
                availableAutoUpdate.reset();
                if (remote > current) {
                    // New version available.
                    availableManualUpdate.emplace(remote, url);
                } else {
                    availableManualUpdate.reset();
                }
                ShowManualUpdateNotification(false);
            }, 
            [] { LOG(Logger::LevelDebug, L"Update pending"); },
            []{
                ShowMessageBox(strRes->Load(IDS_UPDATE_CHECK_FAILED).c_str(), strRes->Load(IDS_APP_TITLE).c_str(), MB_ICONERROR);
            }
        );
        return 0;
    case UM_AUTO_UPDATE_CHECK_DONE:
        OnUpdateCheckDone(wParam, lParam,
            [](const WSemVer& current, const WSemVer& remote, const std::wstring& url) {

                // Save last success time for time throttling.
                try {
                    auto& state = appState->GetState();
                    auto& autoUpdate = state[STATE_AUTO_UPDATE];
                    if (!autoUpdate.is_object()) {
                        autoUpdate = json::object();
                    }
                    autoUpdate[STATE_AUTO_UPDATE_LAST_SUCCESS_TIME] =
                        static_cast<uint64_t>(HoursSinceEpoch(std::chrono::system_clock::now()).count());
                    appState->Commit();
                } catch (...) {
                    LOG(Logger::LevelError, L"Failed to write auto update last success time");
                }

                availableManualUpdate.reset();

                if (remote > current) {
                    // New version available.
                    std::unique_ptr<WSemVer> ignored;
                    const auto& state = appState->GetState();
                    const auto autoUpdate = state.find(STATE_AUTO_UPDATE);
                    if (autoUpdate != state.end() && autoUpdate->is_object()) {
                        const auto ignoreVer = autoUpdate->find(STATE_AUTO_UPDATE_IGNORE);
                        if (ignoreVer != autoUpdate->end()) {
                            try {
                                // Parse ignored version.
                                const std::string ignoredVer = *ignoreVer;
                                ignored = parseSemVer(FromUTF8(reinterpret_cast<const char8_t*>(ignoredVer.data()), ignoredVer.length()));
                            } catch (...) {
                                LOG(Logger::LevelDebug, L"Failed to read ignored version. Malformed data? Ignore");
                            }
                        }
                    }
                    if (ignored && *ignored >= remote) {
                        availableAutoUpdate.reset();
                        return; // Ignore this version.
                    }
                    availableAutoUpdate.emplace(remote, url);
                    ShowAutoUpdateNotification(false);
                } else {
                    availableAutoUpdate.reset();
                }
            },
            [] { LOG(Logger::LevelDebug, L"Update pending when auto update"); },
            [] { LOG(Logger::LevelWarn, L"Auto update failed"); }
        );
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code that uses hdc here...
        EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        StopThemeWatcher();
        applySystemColorModeDebouncer.Cancel();
        autoUpdateStartupDebouncer.Cancel();
        CancelUpdateCheck();
        WriteConfig();
        deviceStateChangedDebouncer.Cancel();
        defaultDeviceChangedDebouncer.Cancel();
        mutedStatedChangedDebouncer.Cancel();
        RemoveMainNotification();
        RemoveManualUpdateNotification();
        RemoveAutoUpdateNotification();
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
                info.fState = processElevated ? MFS_DISABLED : MFS_ENABLED;
                info.dwTypeData = (LPWSTR)strRes->Load(processElevated ? IDS_RUNNING_AS_ADMIN : IDS_RUN_AS_ADMIN).c_str();
                if (!SetMenuItemInfoW(menu, ID_HELP_RUN_AS_ADMIN, FALSE, &info)) {
                    LOG_LAST_ERROR();
                }
                break;
            }
            if (menu == updateMenu) {
                MENUITEMINFO info = { sizeof(info) };
                info.fMask = MIIM_STATE | MIIM_STRING;
                const bool checkingForUpdate = CheckingForUpdate();
                info.fState = checkingForUpdate ? MFS_DISABLED : MFS_ENABLED;
                info.dwTypeData = (LPWSTR)strRes->Load(checkingForUpdate ? IDS_CHECKING_FOR_UPDATES : IDS_CHECK_FOR_UPDATES).c_str();
                if (!SetMenuItemInfoW(menu, ID_MENU_CHECK_FOR_UPDATES, FALSE, &info)) {
                    LOG_LAST_ERROR();
                }
                CheckMenuItem(menu, ID_MENU_AUTO_CHECK_FOR_UPDATES, MF_BYCOMMAND | (autoCheckForUpdates ? MF_CHECKED : MF_UNCHECKED));
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
    case WM_SETTINGCHANGE: {
        // Fallback for when the theme watcher could not open the registry key:
        // the shell broadcasts this message on colour mode changes. It only
        // names the settings area, so ApplySystemColorMode() still re-reads the
        // registry. The section names are undocumented, and lParam is NULL or
        // empty when more than one setting changed, so those are candidates too.
        const wchar_t* section = reinterpret_cast<const wchar_t*>(lParam);
        if (section && *section &&
            wcscmp(section, L"ImmersiveColorSet") != 0 &&
            wcscmp(section, L"WindowsThemeElement") != 0) {
            break;
        }
        applySystemColorModeDebouncer.Emit();
        break;
    }
    }

    if (message == wmTaskbarCreated) {
		// Taskbar created/recreated, add the notification icon again.
        //
        // https://learn.microsoft.com/en-us/windows/win32/shell/taskbar#taskbar-creation-notification

        // Silently add notification icons.
        ShowMainNotification(true);
		ShowManualUpdateNotification(true);
		ShowAutoUpdateNotification(true);
        LOG(Logger::LevelDebug, L"Add notification icons after the taskbar is created");
        return 0;
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

static HFONT hotKeySettingsFont = NULL;

// Message handler for hotkey settings box.
INT_PTR CALLBACK HotKeySettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        // Keep our font when the dialog manager updates fonts on DPI changes.
        ui_font::DisableDpiFontUpdate(hDlg);
        ui_font::RefreshDialogFont(hDlg, hotKeySettingsFont);
        hotKeyInfo.SetToCtrl(GetDlgItem(hDlg, IDC_HOTKEY));
        hotKeySettingWindow = hDlg;
        return TRUE;
    case WM_DPICHANGED:
        // The dialog manager resizes the dialog and re-lays out its controls.
        // Only the font needs to be re-created for the new DPI.
        ui_font::RefreshDialogFont(hDlg, hotKeySettingsFont);
        break;
    case WM_NCDESTROY:
        // Child controls are gone, so the font is no longer in use.
        ui_font::ReleaseDialogFont(hotKeySettingsFont);
        break;
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
				UpdateMainNotification();
                WriteConfig();
            }
            EndDialog(hDlg, 0);
            hotKeySettingWindow = NULL;
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, 0);
            hotKeySettingWindow = NULL;
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static bool SelectSoundFile(HWND owner, std::wstring& path) {
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

static HFONT soundSettingsFont = NULL;

// Message handler for sound settings box.
INT_PTR CALLBACK SoundSettings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    wchar_t buf[1024] = { 0 };
    switch (message)
    {
    case WM_INITDIALOG:
        // Keep our font when the dialog manager updates fonts on DPI changes.
        ui_font::DisableDpiFontUpdate(hDlg);
        ui_font::RefreshDialogFont(hDlg, soundSettingsFont);
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
            EndDialog(hDlg, 0);
            soundSettingsWindow = NULL;
            return (INT_PTR)TRUE;
        } 
        case IDCANCEL:
            EndDialog(hDlg, 0);
			soundSettingsWindow = NULL;
            return (INT_PTR)TRUE;
        }
        break;
    case WM_DPICHANGED:
        // The dialog manager resizes the dialog and re-lays out its controls.
        // Only the font needs to be re-created for the new DPI.
        ui_font::RefreshDialogFont(hDlg, soundSettingsFont);
        break;
    case WM_NCDESTROY:
        // Child controls are gone, so the font is no longer in use.
        ui_font::ReleaseDialogFont(soundSettingsFont);
        break;
    }
    return (INT_PTR)FALSE;
}

static void ShowNotification(UINT id, std::function<void(NOTIFYICONDATAW&)> onPrepareData) {
    NOTIFYICONDATAW data = { 0 };
    data.cbSize = sizeof data;
    data.uVersion = NOTIFYICON_VERSION_4;
    data.hWnd = mainWindow;
    data.uID = id;
    onPrepareData(data);

    // Try to modify.
    auto err = RetryWhileTimeout([&data]() {
            return Shell_NotifyIconW(NIM_MODIFY, &data);
        }, L"NIM_MODIFY");
    if (err == 0) {
        return;
    }
    // Try to add it.
    err = RetryWhileTimeout([&data]() {
        return Shell_NotifyIconW(NIM_ADD, &data);
    }, L"NIM_ADD");
    if (err != 0) {
        LOG_ERROR(err);
        return;
    }
	// Set version.
    err = RetryWhileTimeout([&data]() {
        return Shell_NotifyIconW(NIM_SETVERSION, &data);
	}, L"NIM_SETVERSION");
    if (err != 0) {
        LOG_ERROR(err);
    }
}

static void ShowManualUpdateNotification(bool silent) {
    // Mutual exclusion
    if (!availableManualUpdate) {
        if (!silent) {
            RemoveManualUpdateNotification();
            RemoveAutoUpdateNotification();
            ShowNoUpdateNotification();
        }
        return;
    }

    RemoveNoUpdateNotification();
    RemoveAutoUpdateNotification();

    ShowNotification(MANUAL_UPDATE_NOTIFICATION_ID, 
        [silent](NOTIFYICONDATAW& data) {
            data.uCallbackMessage = UM_MANUAL_UPDATE_NOTIFICATION_NOTIFY;
            data.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(updateIconResID));
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            StringCbCopyW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], strRes->Load(IDS_DEMIC_UPDATE_AVAILABLE).c_str());
            if (!silent) {
                data.uFlags |= NIF_INFO;
                data.dwInfoFlags = NIIF_INFO;
                StringCbCopyW(data.szInfoTitle, sizeof(data.szInfoTitle), strRes->Load(IDS_DEMIC_UPDATE_AVAILABLE).c_str());
                StringCbCopyW(data.szInfo, sizeof(data.szInfo), strRes->Load(IDS_UPDATE_AVAILABLE_TITLE).c_str());
            }
           
        }
    );
}

static void ShowNoUpdateNotification() {
    RemoveAutoUpdateNotification();
    RemoveManualUpdateNotification();
    // No need to interact with the balloon.
    ShowMainNotificationImpl(strRes->Load(IDS_NO_UPDATE));
}

static void RemoveNoUpdateNotification() {
    // No need to interact with the balloon.
    ShowMainNotificationImpl(L"");
}

static void RemoveManualUpdateNotification() {
    RemoveNotification(MANUAL_UPDATE_NOTIFICATION_ID);
}

static void ShowAutoUpdateNotification(bool silent) {
    if (!availableAutoUpdate) {
        if (!silent) {
            RemoveManualUpdateNotification();
            RemoveAutoUpdateNotification();
            ShowNoUpdateNotification();
        }
        return;
    }

    RemoveNoUpdateNotification();
    RemoveManualUpdateNotification();

    ShowNotification(AUTO_UPDATE_NOTIFICATION_ID,
        [silent](NOTIFYICONDATAW& data) {
            data.uCallbackMessage = UM_AUTO_UPDATE_NOTIFICATION_NOTIFY;
            data.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(updateIconResID));
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            StringCbCopyW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], strRes->Load(IDS_DEMIC_UPDATE_AVAILABLE).c_str());
            if (!silent) {
                data.uFlags |= NIF_INFO;
                data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
                StringCbCopyW(data.szInfoTitle, sizeof(data.szInfoTitle), strRes->Load(IDS_DEMIC_UPDATE_AVAILABLE).c_str());
                StringCbCopyW(data.szInfo, sizeof(data.szInfo), strRes->Load(IDS_AUTO_UPDATE_AVAILABLE_TITLE).c_str());
            }
           
        }
    );
}

static void RemoveAutoUpdateNotification() {
    RemoveNotification(AUTO_UPDATE_NOTIFICATION_ID);
}

static UINT MainNotificationIconResID() {
    const auto state = micCtrl->GetMuteState();
    return
        state == MicCtrl::Muted ? micMutedIconResID :
        state == MicCtrl::Unmuted ? micIconResID : noMicIconResID;
}

// Shows the main notification.
// Parameter info is the balloon text; empty means silent.
static void ShowMainNotificationImpl(const std::wstring& info) {
    ShowNotification(MAIN_NOTIFICATION_ID,
        [&info](NOTIFYICONDATAW& data) {
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            if (!info.empty()) {
                data.uFlags |= NIF_INFO;
                StringCbCopyW(data.szInfo, sizeof(data.szInfo), info.c_str());
                StringCbCopyW(data.szInfoTitle, sizeof(data.szInfoTitle), (strRes->Load(IDS_APP_TITLE) + L" " + VERSION).c_str());
                data.dwInfoFlags = NIIF_INFO;
            }
            data.uCallbackMessage = UM_MAIN_NOTIFICATION_NOTIFY;
            data.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(MainNotificationIconResID()));
            std::wstring version = VERSION;
            if (processElevated) {
                version += L" *"; // Indicates running with elevated privilege.
            }
            if (hotKeyInfo.Empty()) {
                wnsprintfW(data.szTip, sizeof data.szTip / sizeof data.szTip[0],
                    strRes->Load(IDS_NOTIFICATION_TIP).c_str(),
                    version.c_str());
            } else {
                wnsprintfW(data.szTip, sizeof data.szTip / sizeof data.szTip[0],
                    strRes->Load(IDS_NOTIFICATION_TIP_HOTKEY).c_str(),
                    version.c_str(), hotKeyInfo.GetStr().c_str());
            }
        }
    );
}

static void ShowMainNotification(bool silent) {
    ShowMainNotificationImpl(silent ? L"" : strRes->Load(IDS_RUNNING_IN_SYSTEM_TRAY));
}

static void UpdateMainNotification() {
    ShowMainNotification(true);
}

static void RemoveNotification(UINT id) {
    NOTIFYICONDATAW data = { 0 };
    data.cbSize = sizeof data;
    data.hWnd = mainWindow;
    data.uID = id;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

static void RemoveMainNotification() {
    RemoveNotification(MAIN_NOTIFICATION_ID);
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
static const auto CONFIG_SIMULATE_VERSION_FOR_UPDATE = L"SimulateVersionForUpdate";
static const auto CONFIG_UPDATES = L"Updates";
static const auto CONFIG_AUTO_CHECK_FOR_UPDATES = L"AutoCheckForUpdates";

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

	simulateNoMicrophone =  GetPrivateProfileIntW(CONFIG_DEBUG, CONFIG_SIMULATE_NO_MICROPHONE, 0, configFilePath.c_str()) == 1;

	buf[0] = 0;
	GetPrivateProfileStringW(CONFIG_DEBUG, CONFIG_PREFERRED_UI_LANGUAGES, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
	preferredUILanguages = buf;
    
    waitForDebugger = GetPrivateProfileIntW(CONFIG_DEBUG, CONFIG_WAIT_FOR_DEBUGGER, 0, configFilePath.c_str()) != 0;

    buf[0] = 0;
    GetPrivateProfileStringW(CONFIG_DEBUG, CONFIG_SIMULATE_VERSION_FOR_UPDATE, L"", buf, sizeof(buf) / sizeof(buf[0]), configFilePath.c_str());
    simulateVersionForUpdateCheck = buf;

    autoCheckForUpdates = GetPrivateProfileIntW(CONFIG_UPDATES, CONFIG_AUTO_CHECK_FOR_UPDATES, 0, configFilePath.c_str()) != 0;
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
    WritePrivateProfileStringW(CONFIG_UPDATES, CONFIG_AUTO_CHECK_FOR_UPDATES,
        std::to_wstring(autoCheckForUpdates).c_str(),
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

std::wstring GetDefaultLogFilePath() {
    std::wstring logFilePath(moduleFilePath);
    const auto sep = logFilePath.find_last_of(L'\\');
    if (sep != std::wstring::npos) {
        logFilePath = logFilePath.substr(0, sep + 1) + LOG_FILE_NAME;
    }
    return logFilePath;
}

#include "pch.h"
#include <prsht.h>
#include "SettingsDialog.h"
#include "HotKeyPlus.h"
#include "resource.h"
#include "../sdk/DemicPluginUtil.h"
#include "MessageWindow.h"

#include "../HotKeyControlInfo.h"

enum ControlID {
    ID_TYPE_COMBO = 0x8000,
};


static INT_PTR SingleHotkeyPageProc(HOTKEY_TYPE type, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NOTIFY: {
            PSHNOTIFY* notify = (PSHNOTIFY*)lParam;
            switch (notify->hdr.code) {
            case PSN_APPLY: {
                if (PropSheet_GetCurrentPageHwnd(GetParent(hwnd)) != hwnd) {
                    break; // Not for this page.
                }

                UnregisterHotKeys();
                config.Type = TYPE_NONE;
                config.Hotkey = config.Hotkey2 = 0;

                HotKeyControlInfo info;
                info.ReadFromCtrl(GetDlgItem(hwnd, IDC_HOTKEY));
                if (info.GetValue()) {
                    if (!RegisterHotKey1(hwnd, info)) {
                        SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID);
                        PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_HOTKEY), TRUE);
                        return TRUE;
                    }
                    config.Type = type;
                    config.Hotkey = info.GetValue();
                }
                WriteConfig();
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            case PSN_SETACTIVE:
                // Set config hotkey value to this page.
                HotKeyControlInfo info;
                info.SetValue(config.Hotkey);
                HWND ctrl = GetDlgItem(hwnd, IDC_HOTKEY);
                info.SetToCtrl(ctrl);
				// Sync combo box selection with page.
				ComboBox_SetCurSel(GetDlgItem(GetParent(hwnd), ID_TYPE_COMBO), GetHotKeyTypeIndex(type));
                break;
            }
        }
    }

    return FALSE;
}

static INT_PTR CALLBACK TogglePageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return SingleHotkeyPageProc(TYPE_TOGGLE, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK PTTPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return SingleHotkeyPageProc(TYPE_PTT, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK PTMPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return SingleHotkeyPageProc(TYPE_PTM, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK OnOffPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NOTIFY: {
            PSHNOTIFY* notify = (PSHNOTIFY*)lParam;
            switch (notify->hdr.code) {
            case PSN_APPLY: {
                    if (PropSheet_GetCurrentPageHwnd(GetParent(hwnd)) != hwnd) {
                        break; // Not for this page.
                    }

                    HotKeyControlInfo infoOn;
                    infoOn.ReadFromCtrl(GetDlgItem(hwnd, IDC_HOTKEY_ON));
                    HotKeyControlInfo infoOff;
                    infoOff.ReadFromCtrl(GetDlgItem(hwnd, IDC_HOTKEY_OFF));
                    if (infoOn.GetValue() == 0 && infoOff.GetValue() != 0) {
                        ShowError(demicHost, demicState, strRes->Load(IDS_ON_OFF_HOTKEY_INVALID).c_str(), hwnd);
                        SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID);
                        PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_HOTKEY_ON), TRUE);
                        return TRUE;
                    }

                    if (infoOn.GetValue() != 0 && infoOff.GetValue() == 0) {
                        ShowError(demicHost, demicState, strRes->Load(IDS_ON_OFF_HOTKEY_INVALID).c_str(), hwnd);
                        SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID);
                        PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_HOTKEY_OFF), TRUE);
                        return TRUE;
                    }

                    UnregisterHotKeys();
                    config.Type = TYPE_NONE;

                    if (infoOn.GetValue() == 0 && infoOff.GetValue() == 0) {
                        config.Hotkey = config.Hotkey2 = 0;
                    } else {
                        if (!RegisterHotKey1(hwnd, infoOn)) {
                            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID);
                            PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_HOTKEY_ON), TRUE);
                            return TRUE;
                        }
                        config.Hotkey = infoOn.GetValue();

                        if (!RegisterHotKey2(hwnd, infoOff)) {
                            UnregisterHotKeys();
                            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID);
                            PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_HOTKEY_OFF), TRUE);
                            return TRUE;
                        }
                        config.Type = TYPE_ON_OFF;
                        config.Hotkey2 = infoOff.GetValue();
                    }

                    WriteConfig();
                    SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                    return TRUE;
                }
            case PSN_SETACTIVE:
                // Set config hotkeys to this page.
                HotKeyControlInfo infoOn;
                infoOn.SetValue(config.Hotkey);
                infoOn.SetToCtrl(GetDlgItem(hwnd, IDC_HOTKEY_ON));
                HotKeyControlInfo infoOff;
                infoOff.SetValue(config.Hotkey2);
                infoOff.SetToCtrl(GetDlgItem(hwnd, IDC_HOTKEY_OFF));
				// Sync combo box selection with page.
                ComboBox_SetCurSel(GetDlgItem(GetParent(hwnd), ID_TYPE_COMBO), GetHotKeyTypeIndex(TYPE_ON_OFF));
                break;
            }
        }
    }

    return FALSE;
}

static void CenterWindow(HWND hwnd) {
    RECT rcWindow = {};
    GetWindowRect(hwnd, &rcWindow);

    MONITORINFO monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    auto left = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - (rcWindow.right - rcWindow.left)) / 2;
    auto top = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - (rcWindow.bottom - rcWindow.top)) / 2;

    SetWindowPos(hwnd, NULL, left, top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

enum {
    UM_POST_CREATE = WM_APP + 1,
    UM_COMMAND_CBN_SELCHANGE,
};

static HHOOK propSheetGetMsgHook = NULL;

static LRESULT CALLBACK PropSheetGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0 || wParam == PM_NOREMOVE) {
        return CallNextHookEx(propSheetGetMsgHook, code, wParam, lParam);
    }
    MSG* msg = (MSG*)lParam;
    if ((msg->message == WM_KEYDOWN || msg->message == WM_KEYUP)
        && (msg->wParam == VK_PRIOR/*PageUp*/ || msg->wParam == VK_NEXT/*PageDown*/
            || msg->wParam == VK_CANCEL/*Ctrl+Scroll or Ctrl+Pause*/)) {
        static wchar_t className[_countof(HOTKEY_CLASSW)] = {};
        int n = GetClassNameW(msg->hwnd, className, _countof(className));
        if (n > 0 && n < _countof(className) 
            && wcscmp(className, HOTKEY_CLASSW) == 0) { //hwnd is a HOTKEY control
            // Skip PropSheet_IsDialogMessage.
            TranslateMessage(msg);
            DispatchMessage(msg);
            msg->message = WM_NULL;
        }
    }
    return CallNextHookEx(propSheetGetMsgHook, code, wParam, lParam);
}

static WNDPROC propSheetOldWndProc = NULL;

static LRESULT WINAPI PropSheetWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam){
    switch (message) {
    case UM_POST_CREATE:
        CenterWindow(hwnd);
		// Setup a message hook to capture certain navigation keys(Ctrl+PageUp/PageDown etc.) before PropSheet processes them,
        // so that we can forward them to the HotKey control.
        propSheetGetMsgHook = SetWindowsHookExW(WH_GETMESSAGE, PropSheetGetMsgProc, NULL, GetCurrentThreadId());
        if (!propSheetGetMsgHook) {
            LOG_LAST_ERROR(demicHost, demicState);
        }
        return 0;
    case WM_DESTROY:
        if (!propSheetGetMsgHook) {
            break;
        }
        if (!UnhookWindowsHookEx(propSheetGetMsgHook)) {
			LOG_LAST_ERROR(demicHost, demicState);
        }
        propSheetGetMsgHook = NULL;
        break;
    case UM_COMMAND_CBN_SELCHANGE:
    case WM_COMMAND:
        if(LOWORD(wParam) == ID_TYPE_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            // Sync page with combo box selection
            auto sel = ComboBox_GetCurSel((HWND)lParam);
            if(sel < 0 || sel > sizeof(hotkeyTypes) / sizeof(hotkeyTypes[0])) {
                break;
			}
			PropSheet_SetCurSel(hwnd, NULL, sel);
        }
		break;
    }
    return CallWindowProcW(propSheetOldWndProc, hwnd, message, wParam, lParam);
}

static int CALLBACK PropSheetCallback(HWND hwnd, UINT msg, LPARAM lParam) {
    if (msg == PSCB_INITIALIZED) {
        propSheetOldWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)PropSheetWndProc);
        if (!propSheetOldWndProc) {
            LOG_LAST_ERROR(demicHost, demicState);
            return 0;
        }
		PostMessage(hwnd, UM_POST_CREATE, 0, 0);

        const auto dpi = GetDpiForWindow(hwnd);
        const auto dialogFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);

        // Hide Tab control
        HWND hTab =  PropSheet_GetTabControl(hwnd);
		RECT tabRect = {};
		GetWindowRect(hTab, &tabRect);
		ScreenToClient(hwnd, (LPPOINT)&tabRect.left);
        ScreenToClient(hwnd, (LPPOINT)&tabRect.right);
        ShowWindow(hTab, SW_HIDE);
		// Create a static text
		auto typeLabel = strRes->Load(IDS_HOTKEY_TYPE);
        SIZE labelSize = {};
        HDC dc = GetDC(hwnd);
        HFONT oldFont = (HFONT)SelectObject(dc, dialogFont);
		GetTextExtentExPointW(dc, typeLabel.c_str(), (int)typeLabel.size(), 0, NULL, NULL, &labelSize);
        SelectObject(dc, oldFont);
        ReleaseDC(hwnd, dc);
        const auto staticLeft = MulDiv(10, dpi, 72);
        HWND hStatic = CreateWindowW(WC_STATIC, typeLabel.c_str(),
            WS_CHILD | WS_VISIBLE,
            staticLeft, tabRect.top, labelSize.cx + MulDiv(1, dpi, 72), labelSize.cy + MulDiv(1, dpi, 72),
			hwnd, NULL, hInstance, NULL);
        SendMessageW(hStatic, WM_SETFONT, (WPARAM)dialogFont, TRUE);
        // Create a DropDown
        HWND hCombo = CreateWindowW(WC_COMBOBOX, L"",
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
            staticLeft + MulDiv(5, dpi, 72) + labelSize.cx, tabRect.top, MulDiv(100, dpi, 72), MulDiv(300, dpi, 72),
            hwnd, (HMENU)ID_TYPE_COMBO, hInstance, NULL);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)dialogFont, TRUE);
        // Bottom align the the label and combo box
		RECT comboRect = {};
		GetWindowRect(hCombo, &comboRect);
		ScreenToClient(hwnd, (LPPOINT)&comboRect.right);
		SetWindowPos(hStatic, NULL, staticLeft, comboRect.bottom - labelSize.cy, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
		// Initialize combo box items
        for (auto str : hotkeyTypeNames) {
            ComboBox_AddString(hCombo, str.c_str());
        }
    }

    return 0;
}


bool SettingsDialog() {
    PROPSHEETPAGEW pages[] = {
        // 0: Toggle
        {
            /*dwSize*/      sizeof(PROPSHEETPAGE),
            /*dwFlags*/     PSP_DEFAULT,
            /*hInstance*/   hInstance,
            /*pszTemplate*/ {MAKEINTRESOURCE(IDD_PROPPAGE_TOGGLE)},
            {0},
            /*pszTitle*/    L"Toggle",
            /*pfnDlgProc*/  TogglePageProc,
        },
        // 1: On/Off
        {
            /*dwSize*/      sizeof(PROPSHEETPAGE),
            /*dwFlags*/     PSP_DEFAULT,
            /*hInstance*/   hInstance,
            /*pszTemplate*/ {MAKEINTRESOURCE(IDD_PROPPAGE_ON_OFF)},
            {0},
            /*pszTitle*/    L"On/Off",
            /*pfnDlgProc*/  OnOffPageProc,
        },
        // 2: PTT
        {
            /*dwSize*/      sizeof(PROPSHEETPAGE),
            /*dwFlags*/     PSP_DEFAULT,
            /*hInstance*/   hInstance,
            /*pszTemplate*/ {MAKEINTRESOURCE(IDD_PROPPAGE_PTT)},
            {0},
            /*pszTitle*/    L"PTT",
            /*pfnDlgProc*/  PTTPageProc,
        },
        // 3: PTM
        {
            /*dwSize*/      sizeof(PROPSHEETPAGE),
            /*dwFlags*/     PSP_DEFAULT,
            /*hInstance*/   hInstance,
            /*pszTemplate*/ {MAKEINTRESOURCE(IDD_PROPPAGE_PTM)},
            {0},
            /*pszTitle*/    L"PTM",
            /*pfnDlgProc*/  PTMPageProc,
        }
    };

    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP | PSH_NOAPPLYNOW | PSH_USECALLBACK;
    psh.hwndParent = demicHost->GetMainWindow(demicState);
    psh.hInstance = hInstance;
    auto caption = std::wstring(demicHost->GetMessageCaption(demicState));
    psh.pszCaption = caption.c_str();
    psh.nPages = sizeof(pages) / sizeof(pages[0]);
    psh.ppsp = pages;
    psh.pfnCallback = PropSheetCallback;
    auto startPageIndex = GetHotKeyTypeIndex(config.Type);
    if (startPageIndex == -1) {
        startPageIndex = 0;
    }
    psh.nStartPage = startPageIndex;
    auto ret = PropertySheetW(&psh);
    return ret != -1;
}
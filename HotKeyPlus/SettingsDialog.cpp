#include "pch.h"
#include <prsht.h>
#include "SettingsDialog.h"
#include "HotKeyPlus.h"
#include "resource.h"
#include "../sdk/DemicPluginUtil.h"
#include "MessageWindow.h"

#include "../HotKeyControlInfo.h"
#include "../UiFont.h"

enum ControlID {
    ID_TYPE_COMBO = 0x8000,
};

// Fonts of the property sheet and of its pages. Same as the font of message boxes.
static HFONT propSheetFont = NULL;
static HFONT togglePageFont = NULL;
static HFONT pttPageFont = NULL;
static HFONT ptmPageFont = NULL;
static HFONT onOffPageFont = NULL;

// The type label and the type combo box created in PropSheetCallback().
static HWND hTypeLabel = NULL;
static HWND hTypeCombo = NULL;
static std::wstring typeLabelText;


// Lays out the type label and the type combo box for the current DPI.
static void LayoutTypeSelector(HWND sheet) {
    if (!hTypeLabel || !hTypeCombo || !propSheetFont) {
        return;
    }
    const UINT dpi = GetDpiForWindow(sheet);
    SIZE labelSize = {};
    HDC dc = GetDC(sheet);
    HGDIOBJ oldFont = SelectObject(dc, propSheetFont);
    GetTextExtentPoint32W(dc, typeLabelText.c_str(), (int)typeLabelText.size(), &labelSize);
    SelectObject(dc, oldFont);
    ReleaseDC(sheet, dc);

    RECT tabRect = {};
    HWND hTab = PropSheet_GetTabControl(sheet);
    GetWindowRect(hTab, &tabRect);
    ScreenToClient(sheet, (LPPOINT)&tabRect.left);
    ScreenToClient(sheet, (LPPOINT)&tabRect.right);

    const int labelLeft = MulDiv(10, dpi, 72);
    SetWindowPos(hTypeCombo, NULL,
        labelLeft + MulDiv(5, dpi, 72) + labelSize.cx, tabRect.top,
        MulDiv(110, dpi, 72), MulDiv(300, dpi, 72),
        SWP_NOZORDER);
    // Bottom align the label and the combo box.
    RECT comboRect = {};
    GetWindowRect(hTypeCombo, &comboRect);
    ScreenToClient(sheet, (LPPOINT)&comboRect.right);
    SetWindowPos(hTypeLabel, NULL,
        labelLeft, comboRect.bottom - labelSize.cy,
        labelSize.cx + MulDiv(1, dpi, 72), labelSize.cy + MulDiv(1, dpi, 72),
        SWP_NOZORDER);
}

static INT_PTR SingleHotkeyPageProc(HOTKEY_TYPE type, HFONT& pageFont, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        // Keep our font when the dialog manager updates fonts on DPI changes.
        ui_font::DisableDpiFontUpdate(hwnd);
        ui_font::RefreshDialogFont(hwnd, pageFont);
        break;
    case WM_DPICHANGED:
        // The dialog manager re-lays out the controls of the page.
        // Only the font needs to be re-created for the new DPI.
        ui_font::RefreshDialogFont(hwnd, pageFont);
        break;
    case WM_NCDESTROY:
        // Child controls are gone, so the font is no longer in use.
        ui_font::ReleaseDialogFont(pageFont);
        break;
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
	return SingleHotkeyPageProc(TYPE_TOGGLE, togglePageFont, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK PTTPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return SingleHotkeyPageProc(TYPE_PTT, pttPageFont, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK PTMPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return SingleHotkeyPageProc(TYPE_PTM, ptmPageFont, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK OnOffPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        // Keep our font when the dialog manager updates fonts on DPI changes.
        ui_font::DisableDpiFontUpdate(hwnd);
        ui_font::RefreshDialogFont(hwnd, onOffPageFont);
        break;
    case WM_DPICHANGED:
        // The dialog manager re-lays out the controls of the page.
        // Only the font needs to be re-created for the new DPI.
        ui_font::RefreshDialogFont(hwnd, onOffPageFont);
        break;
    case WM_NCDESTROY:
        // Child controls are gone, so the font is no longer in use.
        ui_font::ReleaseDialogFont(onOffPageFont);
        break;
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
    case WM_DPICHANGED:
        // The property sheet re-lays out itself and its pages. Only the font and the
        // controls created by PropSheetCallback() need to be updated.
        ui_font::RefreshDialogFont(hwnd, propSheetFont);
        LayoutTypeSelector(hwnd);
        break;
    case WM_NCDESTROY:
        ui_font::ReleaseDialogFont(propSheetFont);
        hTypeLabel = hTypeCombo = NULL;
        break;
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
        SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        propSheetOldWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)PropSheetWndProc);
        if (!propSheetOldWndProc) {
            LOG_LAST_ERROR(demicHost, demicState);
            return 0;
        }
		PostMessage(hwnd, UM_POST_CREATE, 0, 0);

        // Keep our font when the dialog manager updates fonts on DPI changes.
        ui_font::DisableDpiFontUpdate(hwnd);
        ui_font::RefreshDialogFont(hwnd, propSheetFont);

        // Hide Tab control
        HWND hTab =  PropSheet_GetTabControl(hwnd);
        ShowWindow(hTab, SW_HIDE);
		// Create a static text
		typeLabelText = strRes->Load(IDS_HOTKEY_TYPE);
        hTypeLabel = CreateWindowW(WC_STATIC, typeLabelText.c_str(),
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
			hwnd, NULL, hInstance, NULL);
        SendMessageW(hTypeLabel, WM_SETFONT, (WPARAM)propSheetFont, TRUE);
        // Create a COMBOBOX
        hTypeCombo = CreateWindowW(WC_COMBOBOX, L"",
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
            0, 0, 0, 0,
            hwnd, (HMENU)ID_TYPE_COMBO, hInstance, NULL);
        SendMessageW(hTypeCombo, WM_SETFONT, (WPARAM)propSheetFont, TRUE);
		// Initialize combo box items
        for (auto str : hotkeyTypeNames) {
            ComboBox_AddString(hTypeCombo, str.c_str());
        }
        LayoutTypeSelector(hwnd);
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
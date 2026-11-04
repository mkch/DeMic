#include "pch.h"
#include "resource.h"
#include "Billboard.h"
#include "BillboardWnd.h"
#include <commctrl.h>

static const wchar_t* szWindowClass = L"DeMic-Billboard";
static const COLORREF WND_BK_COLOR = COLOR_WINDOW;

static HBITMAP hBmpMicrophone = NULL;
static HBITMAP hBmpMuted = NULL;
static HBITMAP hBmpNoMicrophone = NULL;

static const int IMAGE_SIZE = 512;
static HBRUSH hWndBkBrush = NULL;

// The following layout constants are in logical pixels (96 DPI) and are scaled by DPI when used.
static const int CLIENT_MIN_SIZE = 100;
static const int WND_DEFAULT_WIDTH = 550;
static const int WND_DEFAULT_HEIGHT = 400;
static const int DRAWING_PADDING = 40;
static const int DRAWING_MARGIN = 60;
static const int MIN_DRAWING_SIZE = 100;

// How long the taskbar button stays visible after the window is made prominent.
static const UINT TASKBAR_BUTTON_TIMEOUT_MS = 5000;

static UINT_PTR HideTaskbarButtonTimerId = 0;

void Paint(HWND hWnd, HDC hDC);
void SyncTopMost(HWND hwnd);
void SyncHideCaption(HWND hwnd);
void RestoreLastWindowRect(HWND hwnd);

static RECT rcBeforeMinimized = { 0 };

// Start simulating window dragging.
static void startDrag(HWND hWnd) {
    SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

static bool leftButtonDown = false;
static bool inDragging = false;

// Thickness of the border that responds to resizing. It is also excluded from the
// clickable region, so that clicks meant to resize the window do not toggle the mute state.
static int ResizeBorderThickness(HWND hWnd) {
    const UINT dpi = GetDpiForWindow(hWnd);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

// Converts a RECT between logical pixels (96 DPI) and physical pixels of the given DPI.
static RECT ScaleRect(const RECT& rc, int fromDpi, int toDpi) {
    return { MulDiv(rc.left, toDpi, fromDpi),
             MulDiv(rc.top, toDpi, fromDpi),
             MulDiv(rc.right, toDpi, fromDpi),
             MulDiv(rc.bottom, toDpi, fromDpi) };
}

static int WindowDpi(HWND hWnd) {
    const UINT dpi = GetDpiForWindow(hWnd);
    return dpi == 0 ? USER_DEFAULT_SCREEN_DPI : (int)dpi;
}

// Converts logical pixels (96 DPI) to physical pixels of the given DPI.
static int DipToPx(int dip, int dpi) {
    return MulDiv(dip, dpi, USER_DEFAULT_SCREEN_DPI);
}

// Hit-tests the resizing border. Needed because the non-client area of a window with
// hidden caption is empty, so DefWindowProc no longer reports the border hit-test codes.
static LRESULT HitTestResizeBorder(HWND hWnd, LPARAM lParam) {
    RECT rcWnd = { 0 };
    GetWindowRect(hWnd, &rcWnd);
    const POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
    const int border = ResizeBorderThickness(hWnd);
    const bool left = pt.x < rcWnd.left + border;
    const bool right = pt.x >= rcWnd.right - border;
    const bool top = pt.y < rcWnd.top + border;
    const bool bottom = pt.y >= rcWnd.bottom - border;
    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    return HTCLIENT;
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        ReadConfig();
        RestoreLastWindowRect(hWnd);
        hBmpMicrophone = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_MICROPHONE));
        hBmpMuted = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_MICROPHONE_MUTED));
        hBmpNoMicrophone = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_NO_MICROPHONE));
        hWndBkBrush = CreateSolidBrush(GetSysColor(WND_BK_COLOR));
        SyncTopMost(hWnd);
		SyncHideCaption(hWnd);
        break;
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMinMaxInfo = (LPMINMAXINFO)lParam;

        const int dpi = WindowDpi(hWnd);
        const int clientMinSize = DipToPx(CLIENT_MIN_SIZE, dpi);
        RECT rc = { 0, 0, clientMinSize, clientMinSize };

        if (!hideCaption) {
            // The client area covers the whole window when the caption is hidden.
            AdjustWindowRectExForDpi(&rc,
                GetWindowLong(hWnd, GWL_STYLE),
                GetMenu(hWnd) != NULL,
                GetWindowLong(hWnd, GWL_EXSTYLE),
                dpi);
        }

        lpMinMaxInfo->ptMinTrackSize.x = rc.right - rc.left;
        lpMinMaxInfo->ptMinTrackSize.y = rc.bottom - rc.top;
        break;
    }
    case WM_DESTROY:
        WriteConfig();
        DeleteObject(hBmpMicrophone);
        DeleteObject(hBmpMuted);
        DeleteObject(hBmpNoMicrophone);
        DeleteObject(hWndBkBrush);
        break;
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_LBUTTONDOWN:
        leftButtonDown = true;
        break;
    case WM_LBUTTONUP:{
        leftButtonDown = false;
        RECT rect = { 0 };
        GetClientRect(hWnd, &rect);
        const int border = ResizeBorderThickness(hWnd);
        InflateRect(&rect, -border, -border);
        POINT pt = { 0 };
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        if (PtInRect(&rect, pt)) {
            host->ToggleMuted(state);
        }
        break;
    }
    case WM_MOUSEMOVE:
        if(hideCaption && leftButtonDown && !inDragging) {
			inDragging = true;
            startDrag(hWnd);
            return 0;
		}
        break;
    case WM_NCCALCSIZE:
        if (hideCaption && wParam) {
            if (IsZoomed(hWnd)) {
                // A maximized window is inflated over the work area by the frame thickness.
                LPRECT rc = &((LPNCCALCSIZE_PARAMS)lParam)->rgrc[0];
                const int border = ResizeBorderThickness(hWnd);
                InflateRect(rc, -border, -border);
            }
            // Make the client area cover the whole window, so that there is no
            // non-client area left for DefWindowProc to paint as a dark frame.
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        if (hideCaption) {
            // DefWindowProc paints the default caption when the window is deactivated, without
            // honoring the empty non-client area. Skip it to keep the window borderless.
            return TRUE;
        }
        break;
    case WM_NCHITTEST:
        if (hideCaption) {
            if (inDragging) {
                // The WM_NCHITTEST message right after the last dragging WM_MOUSEMOVE.
                inDragging = leftButtonDown = false;
                break;
            }
            return HitTestResizeBorder(hWnd, lParam);
        }
        break;
    case WM_KILLFOCUS:
        if (hideCaption) {
			// Just for safety, reset the dragging state when the window loses focus.
            inDragging = leftButtonDown = false;
        }
        break;
    case WM_RBUTTONUP: {
        const auto hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_CONTEXT_MENU));
        if (!hMenu) {
            LOG_LAST_ERROR(host, state);
            break;
        }
        const auto popupMenu = GetSubMenu(hMenu, 0);
        if (!popupMenu) {
            DestroyMenu(hMenu);
            LOG_LAST_ERROR(host, state);
        }
        CheckMenuItem(popupMenu, ID_ALWAYS_ON_TOP, alwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(popupMenu, ID_HIDE_CAPTION, hideCaption ? MF_CHECKED : MF_UNCHECKED);
        POINT pt = { 0 };
        GetCursorPos(&pt);
        UINT_PTR cmd = TrackPopupMenu(popupMenu,
            TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
            pt.x, pt.y,
            0,
            hWnd, NULL);
        switch (cmd) {
        case ID_ALWAYS_ON_TOP:
            alwaysOnTop = !alwaysOnTop;
            SyncTopMost(hWnd);
            WriteConfig();
            break;
        case ID_HIDE_CAPTION:
            hideCaption = !hideCaption;
            SyncHideCaption(hWnd);
            WriteConfig();
			break;
        case ID_CLOSE:
            SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
        }
        DestroyMenu(hMenu);
        break;
    }
    case WM_SYSCOMMAND:{
        if (wParam == SC_MINIMIZE) {
           GetWindowRect(hWnd, &rcBeforeMinimized); // Save the window RECT before minimizing.
        }
        break;
    }
    case WM_PAINT: {
            PAINTSTRUCT ps = { 0 };
            HDC hDC = BeginPaint(hWnd, &ps);
            Paint(hWnd, hDC);
            EndPaint(hWnd, &ps);
        }
        break;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void RestoreLastWindowRect(HWND hwnd) {
    if (lastWindowRect.right - lastWindowRect.left <= 0 || lastWindowRect.bottom - lastWindowRect.top <= 0) {
        return;
    }
    // lastWindowRect is saved in logical pixels (96 DPI).
    const RECT rc = ScaleRect(lastWindowRect, USER_DEFAULT_SCREEN_DPI, WindowDpi(hwnd));
    MoveWindow(hwnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, FALSE);
}

// Sync the top most state with alwyasOnTop flag.
void SyncTopMost(HWND hwnd) {
    if (!SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)) {
		LOG_LAST_ERROR(host, state);
    }
}

static void HideWindowCaption(HWND hWnd) {
    SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) & ~WS_CAPTION);
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static void ShowWindowCaption(HWND hWnd) {
	auto x = GetWindowLongPtr(hWnd, GWL_STYLE);
	bool hidden = (x & WS_CAPTION) == 0;
    SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) | WS_CAPTION);
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (hidden) {
        RECT rc = { 0 };
        GetClientRect(hWnd, &rc);
		ClientToScreen(hWnd, (LPPOINT)&rc.left);
        ClientToScreen(hWnd, (LPPOINT)&rc.right);
        AdjustWindowRectExForDpi(&rc, 
            GetWindowLong(hWnd, GWL_STYLE), 
            GetMenu(hWnd) != NULL,
            GetWindowLong(hWnd, GWL_EXSTYLE),
            GetDpiForWindow(hWnd));
		MoveWindow(hWnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    }
}

// Sync the caption state with hideCaption flag.
void SyncHideCaption(HWND hwnd) {
	hideCaption ? HideWindowCaption(hwnd) : ShowWindowCaption(hwnd);
}

// Paints the window.
static void Paint(HWND hWnd, HDC hDC) {
    const int dpi = WindowDpi(hWnd);
    const int padding = DipToPx(DRAWING_PADDING, dpi);
    const int margin = DipToPx(DRAWING_MARGIN, dpi);
    const int minDrawingSize = DipToPx(MIN_DRAWING_SIZE, dpi);
    const int maxDrawingSize = DipToPx(IMAGE_SIZE, dpi);

    RECT rect = { 0 };
    GetClientRect(hWnd, &rect);
    const int cx = rect.right - rect.left - padding * 2;
    const int cy = rect.bottom - rect.top - padding * 2;
    const int drawingSize = max(minDrawingSize, min(maxDrawingSize, min(cx - margin, cy - margin)));
    const int drawingX = padding + (cx - drawingSize) / 2;
    const int drawingY = padding + (cy - drawingSize) / 2;

    HDC hMemDC = CreateCompatibleDC(hDC);
    const auto state = host->GetMuteState();
    HBITMAP hImage = state == StateMuted ? hBmpMuted : state == StateUnmuted ? hBmpMicrophone : hBmpNoMicrophone;
    HGDIOBJ hOldBmp = SelectObject(hMemDC, hImage);

    HBITMAP hBufBmp = CreateCompatibleBitmap(hDC, rect.right - rect.left, rect.bottom - rect.top);
    HDC hBufDC = CreateCompatibleDC(hDC);
    HGDIOBJ hOldBufBmp = SelectObject(hBufDC, hBufBmp);

    FillRect(hBufDC, &rect, hWndBkBrush);
    TransparentBlt(hBufDC,
        drawingX, drawingY, drawingSize, drawingSize,
        hMemDC, 0, 0, IMAGE_SIZE, IMAGE_SIZE,
        RGB(0xFF, 0xFF, 0xFF));
    BitBlt(hDC, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        hBufDC, rect.left, rect.top, SRCCOPY);

    SelectObject(hBufDC, hOldBufBmp);
    DeleteDC(hBufDC);
    DeleteObject(hBufBmp);

    SelectObject(hMemDC, hOldBmp);
    DeleteDC(hMemDC);
}


static ATOM MyRegisterClass() {
    WNDCLASSEXW wcex = { 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = szWindowClass;
    //wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DEMIC));
    //wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_DEMIC));

    return RegisterClassExW(&wcex);
}

static HWND BillboardWnd = NULL;

BOOL CreateBillboardWnd() {
    if (!MyRegisterClass()) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }
    const HWND owner = host->GetMainWindow(state);
    const int dpi = owner ? WindowDpi(owner) : (int)GetDpiForSystem();
    BillboardWnd = CreateWindowW(szWindowClass, strRes->Load(IDS_BILLBOARD).c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DipToPx(WND_DEFAULT_WIDTH, dpi), DipToPx(WND_DEFAULT_HEIGHT, dpi),
        owner, nullptr, hInstance, nullptr);

    if (!BillboardWnd) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }
    return TRUE;
}

void DestroyBillboardWnd() {
    if (BillboardWnd) {
        if (HideTaskbarButtonTimerId) {
            KillTimer(nullptr, HideTaskbarButtonTimerId);
            HideTaskbarButtonTimerId = 0;
        }
        DestroyWindow(BillboardWnd);
        UnregisterClassW(szWindowClass, hInstance);
    }
    BillboardWnd = NULL;
}

// Shows or hides the taskbar button of BillboardWnd. The window is owned by the main window,
// so it has no taskbar button unless WS_EX_APPWINDOW is set.
static void SetTaskbarButtonVisible(BOOL visible) {
    const LONG_PTR exStyle = GetWindowLongPtr(BillboardWnd, GWL_EXSTYLE);
    const LONG_PTR newExStyle = visible ? (exStyle | WS_EX_APPWINDOW) : (exStyle & ~WS_EX_APPWINDOW);
    if (newExStyle == exStyle) {
        return;
    }
    SetWindowLongPtr(BillboardWnd, GWL_EXSTYLE, newExStyle);
    SetWindowPos(BillboardWnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void CALLBACK HideTaskbarButtonTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (HideTaskbarButtonTimerId) {
        KillTimer(nullptr, HideTaskbarButtonTimerId);
        HideTaskbarButtonTimerId = 0;
    }
    SetTaskbarButtonVisible(FALSE);
}

// MakeProminent position BillboardWnd to a prominent position.
static void MakeProminent() {
    if (IsIconic(BillboardWnd)) {
        ShowWindow(BillboardWnd, SW_RESTORE);
    }
    RECT rcWnd = { 0 };
    if (!GetWindowRect(BillboardWnd, &rcWnd)) {
        LOG_LAST_ERROR(host, state);
        return;
    }
    const int cx = rcWnd.right - rcWnd.left;
    const int cy = rcWnd.bottom - rcWnd.top;
    MONITORINFO monitorInfo = { sizeof(monitorInfo), 0 };
    GetMonitorInfo(MonitorFromRect(&rcWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    const LPRECT rcMonitor = &monitorInfo.rcWork;
    if (rcMonitor->right - rcWnd.left < cx) {
        rcWnd.left = rcMonitor->right - cx;
        rcWnd.right = rcWnd.left + cx;
    }
    if (rcWnd.right - rcMonitor->left < cx) {
        rcWnd.right = rcMonitor->left + cx;
        rcWnd.left = rcWnd.right - cx;
    }
    if (rcMonitor->bottom - rcWnd.top < cy) {
        rcWnd.top = rcMonitor->bottom - cy;
        rcWnd.bottom = rcWnd.top + cy;
    }
    if (rcWnd.bottom - rcMonitor->top < cy) {
        rcWnd.bottom = rcMonitor->top + cy;
        rcWnd.top = rcWnd.bottom - cy;
    }
    if (!MoveWindow(BillboardWnd, rcWnd.left, rcWnd.top, cx, cy, TRUE)) {
        LOG_LAST_ERROR(host, state);
    }
    // Flashing is visible in the taskbar only if the window has a taskbar button.
    SetTaskbarButtonVisible(TRUE);
    SetForegroundWindow(BillboardWnd);
    FLASHWINFO flashInfo = { sizeof(flashInfo), BillboardWnd, FLASHW_ALL, 2, 0 };
    FlashWindowEx(&flashInfo);

    // The exact end of the flashing cannot be detected, so remove the taskbar button later.
    if (HideTaskbarButtonTimerId) {
        KillTimer(nullptr, HideTaskbarButtonTimerId);
    }
    HideTaskbarButtonTimerId = SetTimer(nullptr, 0, TASKBAR_BUTTON_TIMEOUT_MS, HideTaskbarButtonTimerProc);
}

void ShowBillboardWnd() {
    if (IsWindowVisible(BillboardWnd)) {
        MakeProminent();
    }
    ShowWindow(BillboardWnd, SW_SHOW);
}

void InvalidateBillboardWnd() {
    InvalidateRect(BillboardWnd, NULL, TRUE);
}

// Gets the window rect in logical pixels (96 DPI).
void GetBillboardWndLogicalRect(LPRECT pRect) {
    RECT rc = { 0 };
    if (IsIconic(BillboardWnd)) {
        rc = rcBeforeMinimized;
    } else {
        GetWindowRect(BillboardWnd, &rc);
    }
    *pRect = ScaleRect(rc, WindowDpi(BillboardWnd), USER_DEFAULT_SCREEN_DPI);
}
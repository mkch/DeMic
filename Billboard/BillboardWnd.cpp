#include "pch.h"
#include "resource.h"
#include "Billboard.h"
#include "BillboardWnd.h"

static const wchar_t* szWindowClass = L"DeMic-Billboard";
static const COLORREF WND_BK_COLOR = COLOR_WINDOW;

static HBITMAP hBmpMicrophone = NULL;
static HBITMAP hBmpMuted = NULL;

static const int IMAGE_SIZE = 512;
static HBRUSH hWndBkBrush = NULL;

void Paint(HWND hWnd, HDC hDC);
void SyncTopMost(HWND hwnd);
void RestoreLastWindowRect(HWND hwnd);

static RECT rcBeforeMinized = { 0 };

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        ReadConfig();
        RestoreLastWindowRect(hWnd);
        hBmpMicrophone = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_MICROPHONE));
        hBmpMuted = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_MICROPHONE_MUTED));
        hWndBkBrush = CreateSolidBrush(GetSysColor(WND_BK_COLOR));
        SyncTopMost(hWnd);
        break;
    case WM_DESTROY:
        WriteConfig();
        DeleteObject(hBmpMicrophone);
        DeleteObject(hBmpMuted);
        DeleteObject(hWndBkBrush);
        break;
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_LBUTTONDOWN: {
        static const int PADDING = 4;
        RECT rect = { 0 };
        GetClientRect(hWnd, &rect);
        InflateRect(&rect, -PADDING*2, -PADDING*2);
        POINT pt = { 0 };
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        if (PtInRect(&rect, pt)) {
            host->ToggleMuted(state);
        }
        break;
    }
    case WM_RBUTTONDOWN: {
        const auto hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_CONTEXT_MENU));
        if (!hMenu) {
            SHOW_LAST_ERROR();
            break;
        }
        const auto popupMenu = GetSubMenu(hMenu, 0);
        if (!popupMenu) {
            DestroyMenu(hMenu);
            SHOW_LAST_ERROR();
        }
        CheckMenuItem(popupMenu, ID_ALWAYS_ON_TOP, alwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
        POINT pt = { 0 };
        GetCursorPos(&pt);
        UINT_PTR cmd = TrackPopupMenu(popupMenu,
            TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
            pt.x, pt.y,
            0,
            hWnd, NULL);
        if (cmd == ID_ALWAYS_ON_TOP) {
            alwaysOnTop = !alwaysOnTop;
            SyncTopMost(hWnd);
            WriteConfig();
        }
        DestroyMenu(hMenu);
        break;
    }
    case WM_SYSCOMMAND:{
        if (wParam == SC_MINIMIZE) {
           GetWindowRect(hWnd, &rcBeforeMinized); // Save the winddow RECT before minizing.
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
    MoveWindow(hwnd, lastWindowRect.left, lastWindowRect.top, 
        lastWindowRect.right - lastWindowRect.left, 
        lastWindowRect.bottom - lastWindowRect.top, FALSE);
}

// Sync the top most state with alwyasOnTop flag.
void SyncTopMost(HWND hwnd) {
       SetWindowPos(hwnd, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

// Paints the window.
static void Paint(HWND hWnd, HDC hDC) {
    static const int PADDING = 40;
    RECT rect = { 0 };
    GetClientRect(hWnd, &rect);
    const int cx = rect.right - rect.left - PADDING * 2;
    const int cy = rect.bottom - rect.top - PADDING * 2;
    const int drawingSize = max(100, min(IMAGE_SIZE, min(cx - 60, cy - 60)));
    const int drawingX = PADDING + (cx - drawingSize) / 2;
    const int drawingY = PADDING + (cy - drawingSize) / 2;

    HDC hMemDC = CreateCompatibleDC(hDC);
    HBITMAP hImage = host->IsMuted() ? hBmpMuted : hBmpMicrophone;
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


ATOM MyRegisterClass() {
    WNDCLASSEXW wcex = { 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(WND_BK_COLOR + 1);
    wcex.lpszClassName = szWindowClass;
    //wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DEMIC));
    //wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_DEMIC));

    return RegisterClassExW(&wcex);
}

static HWND BillboardWnd = NULL;

BOOL CreateBillboardWnd() {
    if (!MyRegisterClass()) {
        SHOW_LAST_ERROR();
        return FALSE;
    }
    BillboardWnd = CreateWindowW(szWindowClass, strRes->Load(IDS_BILLBOARD).c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 550, 400, nullptr, nullptr, hInstance, nullptr);

    if (!BillboardWnd) {
        SHOW_LAST_ERROR();
        return FALSE;
    }
    return TRUE;
}

void DestroyBillboardWnd() {
    if (BillboardWnd) {
        DestroyWindow(BillboardWnd);
        UnregisterClassW(szWindowClass, hInstance);
    }
    BillboardWnd = NULL;
}

// MakeProminent position BillboardWnd to a prominent position.
void MakeProminent() {
    if (IsIconic(BillboardWnd)) {
        ShowWindow(BillboardWnd, SW_RESTORE);
    }
    RECT rcWnd = { 0 };
    if (!GetWindowRect(BillboardWnd, &rcWnd)) {
        SHOW_LAST_ERROR();
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
        SHOW_LAST_ERROR();
    }
    SetForegroundWindow(BillboardWnd);
    FLASHWINFO flashInfo = { sizeof(flashInfo), BillboardWnd, FLASHW_ALL, 2, 0 };
    FlashWindowEx(&flashInfo);
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

void GetBillboardWndRect(LPRECT pRect) {
    if (IsIconic(BillboardWnd)) {
        CopyRect(pRect, &rcBeforeMinized);
        return;
    }
    GetWindowRect(BillboardWnd, pRect);
}
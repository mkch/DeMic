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
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        hBmpMicrophone = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_MICROPHONE));
        hBmpMuted = LoadBitmapW(hInstance, MAKEINTRESOURCEW(IDB_MICROPHONE_MUTED));
        hWndBkBrush = CreateSolidBrush(GetSysColor(WND_BK_COLOR));
        break;
    case WM_DESTROY:
        DeleteObject(hBmpMicrophone);
        DeleteObject(hBmpMuted);
        DeleteObject(hWndBkBrush);
        break;
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_LBUTTONDOWN:
        host->ToggleMuted(state);
        break;
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
        return FALSE;
    }
    wchar_t* title = NULL;
    LoadStringW(hInstance, IDS_BILLBOARD, (LPWSTR)&title, 0);
    BillboardWnd = CreateWindowW(szWindowClass, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 550, 400, nullptr, nullptr, hInstance, nullptr);

    if (!BillboardWnd) {
        return FALSE;
    }
    return TRUE;
}

void ShowBillboardWnd() {
    ShowWindow(BillboardWnd, SW_SHOW);
}

void InvalidateBillboardWnd() {
    InvalidateRect(BillboardWnd, NULL, TRUE);
}

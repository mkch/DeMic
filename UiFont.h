#pragma once

#include <windows.h>

// Font helpers for dialogs of a Per-Monitor v2 aware process.
namespace ui_font {

// Returns the DPI the given window is rendered at.
inline UINT WindowDpi(HWND hwnd) {
	return hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
}

// Converts points to pixels at the given DPI.
inline int PtToPx(int pt, UINT dpi) {
	return MulDiv(pt, (int)dpi, 72);
}

// Creates the font used by message boxes and other system UI, scaled for the DPI of hwnd.
// The caller owns the returned font.
inline HFONT CreateMessageBoxFont(HWND hwnd, int scalePercent = 100) {
	NONCLIENTMETRICSW ncm = { sizeof(ncm) };
	if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
		LOGFONTW lf = {};
		if (!GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf)) {
			return NULL;
		}
		return CreateFontIndirectW(&lf);
	}
	LOGFONTW lf = ncm.lfMessageFont;
	// NONCLIENTMETRICS is reported for the system DPI, which is not necessarily the window's.
	lf.lfHeight = MulDiv(lf.lfHeight, (int)WindowDpi(hwnd), (int)GetDpiForSystem());
	if (scalePercent != 100) {
		lf.lfHeight = MulDiv(lf.lfHeight, scalePercent, 100);
	}
	return CreateFontIndirectW(&lf);
}

// Creates a font of the given point size and face, scaled for the DPI of hwnd.
// The caller owns the returned font.
inline HFONT CreatePointFont(HWND hwnd, int pointSize, const wchar_t* faceName, int weight = FW_NORMAL) {
	return CreateFontW(
		-PtToPx(pointSize, WindowDpi(hwnd)), 0, 0, 0,
		weight, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH, faceName);
}

// Stops the dialog manager from replacing the fonts of the immediate children of hDlg on DPI
// changes, so that fonts applied by the caller survive.
inline void DisableDpiFontUpdate(HWND hDlg) {
	for (HWND child = GetWindow(hDlg, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		SetDialogControlDpiChangeBehavior(child, DCDC_DISABLE_FONT_UPDATE, DCDC_DISABLE_FONT_UPDATE);
	}
}

// Applies font to all immediate children of hDlg.
inline void ApplyChildFonts(HWND hDlg, HFONT font) {
	for (HWND child = GetWindow(hDlg, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
	}
}

} // namespace ui_font

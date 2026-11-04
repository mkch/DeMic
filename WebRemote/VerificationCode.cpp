#include "pch.h"
#include <mutex>
#include <thread>
#include <cmath>

#include "resource.h"
#include "WebRemote.h"
#include "CryptoUtil.h"
#include "../UiFont.h"

static std::mutex codeMutex;
static HWND dialog = NULL;
static std::string verificationCode;

static const size_t CODE_LEN = 6;
static const int CODE_EXPIRATION_SECONDS = 2 * 60;

enum {
	TIMER_COUNTING_DOWN = 1,
};

static std::string GetDisplayString(const std::string& code) {
	if (code.empty()) {
		return std::string(CODE_LEN / 2, '-') + " " + std::string(CODE_LEN / 2, '-');
	}
	return code.substr(0, CODE_LEN / 2) + " " + code.substr(CODE_LEN / 2);
}

// Generates a new verification code.
// Must be called when codeMutex is locked.
static void GenerateNewCode(HWND hwnd) {
	static int elapsedSeconds = 0;
	elapsedSeconds = 0;
	SetTimer(hwnd, TIMER_COUNTING_DOWN, 1000/*1 second*/, [](HWND hwnd, UINT msg, UINT_PTR id, DWORD elapsed) {
		const auto remaining = CODE_EXPIRATION_SECONDS - (++elapsedSeconds);
		if (remaining <= 0) {
			std::lock_guard<std::mutex> guard(codeMutex);
			// Expired.
			KillTimer(hwnd, TIMER_COUNTING_DOWN);
			verificationCode.clear();
			SetDlgItemTextA(hwnd, IDC_VERIFICATION_CODE_STATIC, GetDisplayString(verificationCode).c_str());
			SetDlgItemTextW(hwnd, IDC_COUNT_DOWN_STATIC, strRes->Load(IDS_EXPIRED).c_str());
			return;
		}
		// Not expired.
		SetDlgItemTextW(hwnd, IDC_COUNT_DOWN_STATIC, std::vformat(strRes->Load(IDS_EXPIRE_IN_SEC), std::make_wformat_args(remaining)).c_str());
	});
	verificationCode = GenerateRandomCode<char>("234567ACDEFGHJKLMNPQRSTUVWXY", CODE_LEN);
	SetDlgItemTextA(hwnd, IDC_VERIFICATION_CODE_STATIC, GetDisplayString(verificationCode).c_str());
	SetDlgItemTextW(hwnd, IDC_COUNT_DOWN_STATIC, std::vformat(strRes->Load(IDS_EXPIRE_IN_SEC), std::make_wformat_args(CODE_EXPIRATION_SECONDS)).c_str());
}

enum {
	UM_CODE_USED = WM_USER + 1,
};

static HFONT uiFont = NULL;   // Font of the dialog, same as message boxes.
static HFONT codeFont = NULL; // Font of the verification code.

// Applies the fonts for the current DPI of the dialog.
static void ApplyFonts(HWND hwnd) {
	static const int CODE_FONT_POINT_SIZE = 25;

	if (uiFont) {
		DeleteObject(uiFont);
		uiFont = NULL;
	}
	if (codeFont) {
		DeleteObject(codeFont);
		codeFont = NULL;
	}

	uiFont = ui_font::CreateMessageBoxFont(hwnd);
	if (uiFont) {
		ui_font::ApplyChildFonts(hwnd, uiFont);
	}

	// Falls back to the UI font if the monospace font is unavailable.
	codeFont = ui_font::CreatePointFont(hwnd, CODE_FONT_POINT_SIZE, L"Consolas");
	SendMessageW(GetDlgItem(hwnd, IDC_VERIFICATION_CODE_STATIC), WM_SETFONT,
		(WPARAM)(codeFont ? codeFont : uiFont), TRUE);
}

static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG: {
			std::lock_guard<std::mutex> guard(codeMutex);
			dialog = hwnd;

			std::wstring title = std::format(L"{} - {}", host->GetMessageCaption(state), strRes->Load(IDS_TITLE_VERIFICATION_CODE));
			SetWindowTextW(hwnd, title.c_str());

			// Keep our fonts when the dialog manager updates fonts on DPI changes.
			ui_font::DisableDpiFontUpdate(hwnd);
			ApplyFonts(hwnd);

			GenerateNewCode(hwnd);
			return TRUE;
		}
		case WM_DPICHANGED: {
			// The dialog manager resizes the dialog and re-lays out its controls.
			// Only the fonts need to be re-created for the new DPI.
			ApplyFonts(hwnd);
			break;
		}
		case WM_COMMAND:
			switch (wParam) {
				case IDCANCEL: {
					EndDialog(hwnd, 0);
					return TRUE;
				}
				case IDC_REFRESH_CODE: {
					std::lock_guard<std::mutex> guard(codeMutex);
					GenerateNewCode(hwnd);
					return TRUE;
				}
			}
			return FALSE;

		case UM_CODE_USED: {
			std::lock_guard<std::mutex> guard(codeMutex);
			if (!verificationCode.empty()) {
				break;
			}
			SetDlgItemTextA(hwnd, IDC_VERIFICATION_CODE_STATIC, GetDisplayString(verificationCode).c_str());
			KillTimer(hwnd, TIMER_COUNTING_DOWN);
			SetDlgItemTextW(hwnd, IDC_COUNT_DOWN_STATIC, strRes->Load(IDS_EXPIRED).c_str());
			return TRUE;
		}
		case WM_DESTROY: {
			std::lock_guard<std::mutex> guard(codeMutex);
			verificationCode.clear();
			dialog = NULL;
			return TRUE;
		}
		case WM_NCDESTROY:
			dialog = NULL;
			// Child controls are gone, so the fonts are no longer in use.
			if (uiFont) {
				DeleteObject(uiFont);
				uiFont = NULL;
			}
			if (codeFont) {
				DeleteObject(codeFont);
				codeFont = NULL;
			}
			return TRUE;
	}
	return FALSE;
}

void ShowVerificationCodeDialog() {
	DialogBoxW(hInstance, MAKEINTRESOURCE(IDD_VERIFICATION_CODE_DIALOG), host->GetMainWindow(state), DlgProc);
}

void DestroyVerificationCodeDialog() {
	if (dialog) {
		DestroyWindow(dialog);
	}
}

// Verify the code. If it's correct, clear the code and return true. Otherwise return false.
// This function can be called in any thread.
bool VerifyCode(const std::string& code) {
	std::lock_guard<std::mutex> guard(codeMutex);
	if(dialog == NULL || verificationCode.empty()) {
		return false;
	}
	if(code == verificationCode) {
		verificationCode.clear();
		PostMessage(dialog, UM_CODE_USED, 0, 0);
		return true;
	}
	return false;
}
#include "pch.h"
#include <mutex>
#include <thread>

#include "resource.h"
#include "WebRemote.h"
#include "CryptoUtil.h"

static std::mutex codeMutex;
static HWND dialog = NULL;
static std::string verificationCode;

static std::string GenerateRandomCode() {
	static const size_t CODE_LEN = 6;
	constexpr std::string_view candidateChars = "234567ACDEFGHJKMNPRSTUWXY";
	return GenerateRandomCode<char>(candidateChars, CODE_LEN);
}

static std::string GetDisplayString(const std::string& code) {
	if(code.length() <= 3) {
		return code;
	}
	return code.substr(0, 3) + " " + GetDisplayString(code.substr(3));
}


enum {
	UM_REFRESH_CODE = WM_USER + 1,
};

static HFONT codeFont = NULL;

static void ApplyFont(HWND hwnd, UINT dpi) {
	static const int FONT_SIZE = 35; // in points

	HWND hCtrl = GetDlgItem(hwnd, IDC_VERIFICATION_CODE_STATIC);

	if (codeFont)
		DeleteObject(codeFont);

	int height = -MulDiv(FONT_SIZE, dpi, 72);

	codeFont = CreateFontW(
		height, 0, 0, 0,
		FW_NORMAL,
		FALSE, FALSE, FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		DEFAULT_PITCH,
		L"Consolas");

	if (codeFont == NULL) {
		HFONT hOldFont = (HFONT)SendMessage(hCtrl, WM_GETFONT, 0, 0);
		LOGFONT lf{};
		GetObject(hOldFont, sizeof(lf), &lf);
		lf.lfHeight *= 2;
		codeFont = CreateFontIndirect(&lf);
	}
	SendMessage(hCtrl, WM_SETFONT, (WPARAM)codeFont, TRUE);
}

static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG: {
			ApplyFont(hwnd, GetDpiForWindow(hwnd));

			std::lock_guard<std::mutex> guard(codeMutex);
			dialog = hwnd;
			verificationCode = GenerateRandomCode();
			SetDlgItemTextA(hwnd, IDC_VERIFICATION_CODE_STATIC, GetDisplayString(verificationCode).c_str());
			return TRUE;
		}
		case WM_DPICHANGED: {
			UINT dpiX = LOWORD(wParam);

			RECT* prc = (RECT*)lParam;

			SetWindowPos(hwnd, NULL,
				prc->left, prc->top,
				prc->right - prc->left,
				prc->bottom - prc->top,
				SWP_NOZORDER | SWP_NOACTIVATE);

			ApplyFont(hwnd, dpiX);
			return TRUE;
		}
		case WM_COMMAND:
			switch (wParam) {
				case IDCANCEL: {
					EndDialog(hwnd, 0);
					return TRUE;
				}
				case IDC_REFRESH_CODE: {
					PostMessage(hwnd, UM_REFRESH_CODE, 0, 0);
					return TRUE;
				}
			}
			return FALSE;

		case UM_REFRESH_CODE: {
			std::lock_guard<std::mutex> guard(codeMutex);
			verificationCode = GenerateRandomCode();
			SetDlgItemTextA(hwnd, IDC_VERIFICATION_CODE_STATIC, GetDisplayString(verificationCode).c_str());
			return TRUE;
		}
		case WM_DESTROY: {
			std::lock_guard<std::mutex> guard(codeMutex);
			verificationCode.clear();
			dialog = NULL;
			DeleteObject(codeFont);
			return TRUE;
		}
		case WM_NCDESTROY:
			dialog = NULL;
			return TRUE;
	}
	return 0;
}

void ShowVerificationCodeDialog() {
	DialogBoxW(hInstance, MAKEINTRESOURCE(IDD_VERIFICATION_CODE_DIALOG), host->GetMainWindow(state), DlgProc);
}

void DestroyVerificationCodeDialog() {
	if (dialog) {
		DestroyWindow(dialog);
	}
}

bool VerifyCode(const std::string& code) {
	std::lock_guard<std::mutex> guard(codeMutex);
	if(dialog == NULL || verificationCode.empty()) {
		return false;
	}
	if(code == verificationCode) {
		verificationCode.clear();
		PostMessage(dialog, UM_REFRESH_CODE, 0, 0);
		return true;
	}
	return false;
}
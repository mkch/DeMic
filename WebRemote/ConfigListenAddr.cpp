#include "pch.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <format>
#include <boost/algorithm/string.hpp>

#include "resource.h"
#include "ConfigListenAddr.h"
#include "WebRemote.h"
#include "NetUtil.h"
#include "Server.h"


static HWND dialog = NULL;

// Whetehr the HTTP server is running when launching the dialog.
// Used to determine whether to start the server when user cancels the dialog.
static bool serverRunnintWhenLuanch = false;

static void SetComboHeight(HWND combo, int dpi) {
	int height = MulDiv(100, dpi, 72);
	RECT hostComboRect = {};
	GetWindowRect(combo, &hostComboRect);
	ScreenToClient(dialog, (POINT*)&hostComboRect.left);
	ScreenToClient(dialog, (POINT*)&hostComboRect.right);
	SetWindowPos(combo, nullptr,
		hostComboRect.left, hostComboRect.top,
		hostComboRect.right - hostComboRect.left, height,
		SWP_NOMOVE | SWP_NOZORDER);
}

static bool SelectPemFile(HWND owner, const std::wstring& initDir, std::wstring& path) {
	std::wstring dir;
	if(initDir.empty()) {
		 dir = moduleFilePath.parent_path().wstring();
	} else {
		dir = initDir;
	}
	wchar_t buf[1024] = { 0 };
	OPENFILENAMEW ofn = { 0 };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFilter = L"*.pem\0*.pem\0*.*\0*.*\0";
	ofn.lpstrFile = buf;
	ofn.nMaxFile = sizeof(buf) / sizeof(buf[0]);
	ofn.lpstrInitialDir = dir.c_str();
	ofn.Flags = OFN_FILEMUSTEXIST;
	if (!GetOpenFileNameW(&ofn)) {
		DWORD err = GetLastError();
		if (err != 0) {
			LOG_ERROR(host, state, err);
		}
		return false;
	}
	path = buf;
	return true;
}

static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG: {
			dialog = hwnd;
			serverRunnintWhenLuanch = HTTPServerRunning();
			std::wstring title = std::format(L"{} - {}", host->GetMessageCaption(state), strRes->Load(IDS_TITLE_LISTEN_ADDR));
			SetWindowTextW(hwnd, title.c_str());

			SetDlgItemTextW(hwnd, IDC_LISTEN_ADDR_CONFIG_TIP_STATIC, strRes->Load(IDS_LISTEN_ADDR_CONFIG_TIP).c_str());

			dialog = hwnd;
			HWND hostCombo = GetDlgItem(hwnd, IDC_LISTEN_HOST_COMBO);
			SetComboHeight(hostCombo, GetDpiForWindow(hwnd));
			// Set current listen host.
			SetWindowTextA(hostCombo, config.ServerListenHost.c_str());
			// Config comobo list.
			DWORD ec = 0;
			auto addresses = net_util::GetAllBindableAddresses(ec);
			if (!ec && !addresses.empty()) {
				std::sort(addresses.begin(), addresses.end(), [](const std::string& a, const std::string& b) {
					return a.length() < b.length();
					});
				for (auto const& address : addresses) {
					SendMessageA(hostCombo, CB_ADDSTRING, 0, (LPARAM)address.c_str());
				}
			}
			// Set current listen port.
			SetDlgItemTextA(hwnd, IDC_LISTEN_PORT_EDIT, config.ServerListenPort.c_str());
			// Config HTTPS settings contorls.
			CheckDlgButton(hwnd, IDC_ENABLE_HTTPS_CHECK, config.EnableHTTPS ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextW(hwnd, IDC_CERT_FILE_PATH_TEXT, FromUTF8(std::u8string_view((const char8_t*)config.HTTPSConfig.CertPemFilePath.data(), config.HTTPSConfig.CertPemFilePath.size())).c_str());
			SetDlgItemTextW(hwnd, IDC_KEY_FILE_PATH_TEXT, FromUTF8(std::u8string_view((const char8_t*)config.HTTPSConfig.KeyPemFilePath.data(), config.HTTPSConfig.KeyPemFilePath.size())).c_str());
			EnableWindow(GetDlgItem(hwnd, IDC_CERT_FILE_PATH_TEXT), config.EnableHTTPS);
			EnableWindow(GetDlgItem(hwnd, IDC_KEY_FILE_PATH_TEXT), config.EnableHTTPS);
			EnableWindow(GetDlgItem(hwnd, IDC_SELECT_CERT_FILE_BUTTON), config.EnableHTTPS);
			EnableWindow(GetDlgItem(hwnd, IDC_SELECT_KEY_FILE_BUTTON), config.EnableHTTPS);
			return TRUE;
		}
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDC_CERT_FILE_PATH_TEXT:
					if (HIWORD(wParam) == STN_DBLCLK) {
						SetWindowTextW((HWND)lParam, L"");
					}
					return TRUE;
				case IDC_KEY_FILE_PATH_TEXT:
					if (HIWORD(wParam) == STN_DBLCLK) {
						SetWindowTextW((HWND)lParam, L"");
					}
					return TRUE;
				case IDC_ENABLE_HTTPS_CHECK: {
					config.EnableHTTPS = IsDlgButtonChecked(hwnd, IDC_ENABLE_HTTPS_CHECK) == BST_CHECKED;
					EnableWindow(GetDlgItem(hwnd, IDC_CERT_FILE_PATH_TEXT), config.EnableHTTPS);
					EnableWindow(GetDlgItem(hwnd, IDC_KEY_FILE_PATH_TEXT), config.EnableHTTPS);
					EnableWindow(GetDlgItem(hwnd, IDC_SELECT_CERT_FILE_BUTTON), config.EnableHTTPS);
					EnableWindow(GetDlgItem(hwnd, IDC_SELECT_KEY_FILE_BUTTON), config.EnableHTTPS);
					return TRUE;
				}
				case IDC_SELECT_CERT_FILE_BUTTON: {
					std::wstring certPath;
					wchar_t buf[1024] = { 0 };
					GetDlgItemTextW(hwnd, IDC_CERT_FILE_PATH_TEXT, buf, sizeof(buf) / sizeof(buf[0]));
					if (SelectPemFile(hwnd, std::filesystem::path(buf).parent_path().wstring(), certPath)) {
						SetDlgItemTextW(hwnd, IDC_CERT_FILE_PATH_TEXT, certPath.c_str());
					}
					return TRUE;
				}
				case IDC_SELECT_KEY_FILE_BUTTON: {
					std::wstring keyPath;
					wchar_t buf[1024] = { 0 };
					GetDlgItemTextW(hwnd, IDC_KEY_FILE_PATH_TEXT, buf, sizeof(buf) / sizeof(buf[0]));
					if (SelectPemFile(hwnd, std::filesystem::path(buf).parent_path().wstring(), keyPath)) {
						SetDlgItemTextW(hwnd, IDC_KEY_FILE_PATH_TEXT, keyPath.c_str());
					}
					return TRUE;
				}
				case IDCANCEL: {
					if (!HTTPServerRunning() && serverRunnintWhenLuanch) {
						// No configuration saved. Starts server with the old configuration.
						StartHTTPServerWithPrompt(config, hwnd);
					}
					EndDialog(hwnd, 0);
					return TRUE;
				}
				case IDOK: {
					auto oldConfig = config;
					wchar_t buf[1024] = { 0 };
					GetDlgItemTextW(hwnd, IDC_LISTEN_HOST_COMBO, buf, sizeof(buf)/sizeof(buf[0]));
					auto u8 = ToUTF8(boost::trim_copy(std::wstring(buf)));
					config.ServerListenHost = std::string((const char*)u8.data(), u8.size());
					GetDlgItemTextW(hwnd, IDC_LISTEN_PORT_EDIT, buf, sizeof(buf)/sizeof(buf[0]));
					u8 = ToUTF8(boost::trim_copy(std::wstring(buf)));
					config.ServerListenPort = std::string((const char*)u8.data(), u8.size());
					config.EnableHTTPS = IsDlgButtonChecked(hwnd, IDC_ENABLE_HTTPS_CHECK) == BST_CHECKED;
					GetDlgItemTextW(hwnd, IDC_CERT_FILE_PATH_TEXT, buf, sizeof(buf)/sizeof(buf[0]));
					u8 = ToUTF8(buf);
					config.HTTPSConfig.CertPemFilePath = std::string((const char*)u8.data(), u8.size());
					GetDlgItemTextW(hwnd, IDC_KEY_FILE_PATH_TEXT, buf, sizeof(buf)/sizeof(buf[0]));
					u8 = ToUTF8(buf);
					config.HTTPSConfig.KeyPemFilePath = std::string((const char*)u8.data(), u8.size());;
					StopHTTPServer();
					if (!StartHTTPServerWithPrompt(config, hwnd)) {
						config = oldConfig; // Restore old config if failed to start server with new config.
						return TRUE;
					}
					// Save config and exit dialog.
					// Retreive the actual listen  port from running server,
					// in case the server resolved it to a different value (e.g. "HTTP" to 80).
					config.ServerListenPort = std::to_string(GetHTTPServerListenPort());
					if (!config.Enabled) {
						StopHTTPServer();
					}
					WriteConfig();
					EndDialog(hwnd, 0);
					return TRUE;
				}
			}
			break;
		case WM_DPICHANGED: {
			UINT dpiX = LOWORD(wParam);

			RECT* prc = (RECT*)lParam;

			SetWindowPos(hwnd, NULL,
				prc->left, prc->top,
				prc->right - prc->left,
				prc->bottom - prc->top,
				SWP_NOZORDER | SWP_NOACTIVATE);

			HWND hostCombo = GetDlgItem(hwnd, IDC_LISTEN_HOST_COMBO);
			SetComboHeight(hostCombo, LOWORD(wParam));
			return TRUE;
		}
		
	}
	return FALSE;
}

void ShowConfigListenAddrDialog() {
	DialogBoxW(hInstance, MAKEINTRESOURCE(IDD_LISTEN_ADDR_DIALOG), host->GetMainWindow(state), DlgProc);
}

void DestroyConfigListenAddrDialog() {
	if (dialog) {
		DestroyWindow(dialog);
		dialog = NULL;
	}
}
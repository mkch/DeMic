#include "pch.h"
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

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

static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG: {
			dialog = hwnd;
			serverRunnintWhenLuanch = HTTPServerRunning();
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
			break;
		}
		case WM_COMMAND:
			switch (wParam) {
				case IDCANCEL: {
					if (!HTTPServerRunning() && serverRunnintWhenLuanch) {
						// No configuration saved. Starts server with the old configuration.
						StartHTTPServerWithPrompt(config.ServerListenHost, config.ServerListenPort, hwnd);
					}
					EndDialog(hwnd, 0);
					return TRUE;
				}
				case IDOK: {
					char hostBuf[256];
					char portBuf[256];
					GetDlgItemTextA(hwnd, IDC_LISTEN_HOST_COMBO, hostBuf, sizeof(hostBuf));
					GetDlgItemTextA(hwnd, IDC_LISTEN_PORT_EDIT, portBuf, sizeof(portBuf));
					StopHTTPServer();
					if (!StartHTTPServerWithPrompt(hostBuf, portBuf, hwnd)) {
						return TRUE;
					}
					// Save config and exit dialog.
					config.ServerListenHost = hostBuf;
					config.ServerListenPort = portBuf;
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
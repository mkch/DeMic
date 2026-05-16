#pragma once

#include <windows.h>
#include <string>
#include <CommCtrl.h>
#include <sstream>
#include <vector>

// HotKeyControlInfo contains the setting of a HOTKEY control.
class HotKeyControlInfo {
private:
    BYTE vk = 0;        // Current hotkey vk of the hotkey contorl.
    BYTE mod = 0;       // Current hotkey modifier of the hotkey control.
    std::wstring str;   // The string representation of hotkey in UI.
public:
    HotKeyControlInfo() = default;
    HotKeyControlInfo(const HotKeyControlInfo&) = delete;

    bool Empty() const {
        return vk == 0;
    }
    // Get the hot key setting of hot key control.
    void ReadFromCtrl(HWND ctrl) {
        SetValue((WORD)SendMessage(ctrl, HKM_GETHOTKEY, 0, 0));
    }
    // Set the hot key setting to a hot key control.
    void SetToCtrl(HWND ctrl) const {
        SendMessage(ctrl, HKM_SETHOTKEY, GetValue(), 0);
    }
    // ReigsterHotKey registers the hot key represented by this hot key control info to the system.
    bool RegisterHotKey(HWND hwnd, int hotKeyId, bool noRepeat = false) const {
        if (vk == 0) { // No hot key is given.
            return false;
        }
        // Translate modifier.
        UINT modifier = 0;
        if (mod & HOTKEYF_ALT) {
            modifier |= MOD_ALT;
        }
        if (mod & HOTKEYF_CONTROL) {
            modifier |= MOD_CONTROL;
        }
        if (mod & HOTKEYF_SHIFT) {
            modifier |= MOD_SHIFT;
        }
        return ::RegisterHotKey(hwnd, hotKeyId, modifier | (noRepeat ? MOD_NOREPEAT : 0), vk);
    }
    
	// GetVirtualKeys returns all virtual keys represented by this hot key control info.
    std::vector<int> GetVirtualKeys() const {
        std::vector<int> keys;
		keys.push_back(vk);
        if (mod & HOTKEYF_ALT) {
            keys.push_back(VK_MENU);
        }
        if (mod & HOTKEYF_CONTROL) {
            keys.push_back(VK_CONTROL);
        }
        if (mod & HOTKEYF_SHIFT) {
			keys.push_back(VK_SHIFT);
        }
        return keys;
    }

    const std::wstring& GetStr() const {
        return str;
    }

    // Return value of HKM_GETHOTKEY, or parameter of HKM_SETHOTKEY.
    WORD GetValue() const {
        return MAKEWORD(vk, mod);
    }

    void SetValue(WORD value) {
        vk = LOBYTE(value);
        mod = HIBYTE(value);
        str = GetHotKeyString(vk, mod);
    }
private:
    static bool GetVirtualKeyName(UINT vk, bool extended, wchar_t* buffer, size_t bufferSize) {
        auto scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        // GetKeyNameText requires the scan code in the 16-23 bits, 
        // the extended key flag in the 24th bit,
        // and "Do not care" bit in the 25th bit for modifier keys.
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeynametextw
        scan = (scan << 16) | (1 << 25);
        if (extended) {
            scan |= (1 << 24); // Add extended key flag.
        }
        if (GetKeyNameText(scan, buffer, DWORD(bufferSize)) == 0) {
            return false;
        }
        return true;
    }
    // GetHotKeyString returns the display string of the hot key given vk and modifier.
    static std::wstring GetHotKeyString(BYTE vk, BYTE mod) {
        if (vk == 0) {
            return L"";
        }
        std::wstringstream ss;

        static const size_t BUF_SIZE = 64;
        wchar_t buf[BUF_SIZE] = { 0 };

        // Translate modifier.
        if (mod & HOTKEYF_CONTROL) {
            if (!GetVirtualKeyName(VK_CONTROL, false, buf, sizeof(buf) / sizeof(buf[0]))) {
                return L"";
            }
            ss << buf << L" + ";
        }

        if (mod & HOTKEYF_SHIFT) {
            if (!GetVirtualKeyName(VK_SHIFT, false, buf, sizeof(buf) / sizeof(buf[0]))) {
                return L"";
            }
            ss << buf << L" + ";
        }

        if (mod & HOTKEYF_ALT) {
            if (!GetVirtualKeyName(VK_MENU, false, buf, sizeof(buf) / sizeof(buf[0]))) {
                return L"";
            }
            ss << buf << L" + ";
        }

        // Translate vk.
        if (!GetVirtualKeyName(vk, (mod & HOTKEYF_EXT) != 0, buf, sizeof(buf) / sizeof(buf[0]))) {
            return L"";
        }
        ss << buf;
        return ss.str();
    }
};
#pragma once

#include "../HotKeyControlInfo.h"

bool CreateMessageWindow();
void DestroyMessageWindow();
bool RegisterHotKey1(HWND parent, const HotKeyControlInfo&);
bool RegisterHotKey2(HWND parent, const HotKeyControlInfo&);
void UnregisterHotKeys();


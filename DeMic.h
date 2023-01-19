#pragma once

#include "resource.h"

#define _WT(str) L""##str
// Should be:
// #define W__FILE__ L""##__FILE__
// But the linter of VSCode does not like it(bug?).
#define W__FILE__ _WT(__FILE__)

#define SHOW_ERROR(err) ShowError(err, W__FILE__, __LINE__)
#define SHOW_LAST_ERROR() SHOW_ERROR(GetLastError())

extern HMENU popupMenu;

void ShowError(const wchar_t* msg);
void ShowError(const wchar_t* msg, const wchar_t* file, int line);
void ShowError(DWORD lastError, const wchar_t* file, int line);

void TurnOnMic();
void TurnOffMic();
void ToggleMuted();
BOOL IsMuted();

#pragma once

#include "Billboard.h"

BOOL CreateBillboardWnd();
void DestroyBillboardWnd();
void ShowBillboardWnd();
void InvalidateBillboardWnd();
void GetBillboardWndRelativeRect(WindowRectRatio& rect);

// drawing.h

#ifndef DRAWING_H
#define DRAWING_H

#include "wwindows.h"

UINT32_T RGB(UINT8_T red, UINT8_T green, UINT8_T blue);
void DrawRoundedHighlight(HDC hdc, RECT rect, UINT32_T highlightColor, BYTE alpha);
BOOL IsCursorOverWindow(HWND hWnd);
void DrawTitleBar(HWND hdc, RECT clientRect);
void ShowCustomMenu(HWND hParentWnd);
#endif

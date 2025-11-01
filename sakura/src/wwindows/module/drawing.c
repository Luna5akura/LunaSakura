// drawing.c

#include "std/stdio.h"
#include "util/mem.h"

#include "wwindows/globals.h"
#include "wwindows.h"

#include "wwindows/drawing.h"

UINT32_T RGB(UINT8_T red, UINT8_T green, UINT8_T blue) {
  return ((UINT32_T)red << 16) | ((UINT32_T)green << 8) | (UINT32_T)blue;
}

void DrawRoundedHighlight(HDC hdc, RECT rect, UINT32_T highlightColor, BYTE alpha) {
  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;

  HDC memDC = CreateCompatibleDC(hdc);
  if (!memDC) {
    return;
  }

  BITMAPINFO bmi = {0};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlane = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *pvBits = NULL;
  HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
  if (!hBitmap) {
    DeleteDC(memDC);
    return;
  }

  HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

  ZeroMemory(pvBits, width * height * 4);

  HBRUSH hBrush= CreateSolidBrush(highlightColor);
  HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hBrush);

  BeginPath(memDC);
  RoundRect(memDC, 0, 0, width, height, 10, 10);
  EndPath(memDC);

  FillPath(memDC);

  SelectObject(memDC, hOldBrush);
  DeleteObject(hBrush);

  BLENDFUNCTION blendFunc = {0};
  blendFunc.BlendOp = AC_SRC_OVER;
  blendFunc.BlendFlags = 0;
  blendFunc.SourceConstantAlpha = alpha;
  blendFunc.AlphaFormat = 0; // TODO: Anti-aliasing

  AlphaBlend(
    hdc,
    rect.left,
    rect.top,
    width,
    height,
    memDC,
    0,
    0,
    width,
    height,
    blendFunc
  );

  SelectObject(memDC, hOldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(memDC);
}

BOOL IsCursorOverWindow(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    RECT rect;
    GetWindowRect(hWnd, &rect);
    return PtInRect(&rect, pt);
}

void DrawTitleBar(HWND hdc, RECT clientRect) {
  RECT titleBarRect = { clientRect.left, clientRect.top, clientRect.right, clientRect.top + TITLE_BAR_HEIGHT};
  HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
  FillRect(hdc, &titleBarRect, hBrush);
  DeleteObject(hBrush);

  SetTextColor(hdc, RGB(255, 255, 255));
  SetBkMode(hdc, TRANSPARENT);
  RECT closeButtonRect = {clientRect.right - 30, clientRect.top, clientRect.right, clientRect.top + TITLE_BAR_HEIGHT};

  RECT menuButtonRect = { clientRect.left + 4, clientRect.top, clientRect.left + 34, clientRect.top + TITLE_BAR_HEIGHT};
  if (mouseHover == MH_MENU_BUTTON) {
    DrawRoundedHighlight(hdc, menuButtonRect, RGB(255, 255, 255), 128);
  }
  DrawTextW(hdc, L"≡", -1, &menuButtonRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  DrawTextW(hdc, L"Sakura", -1, &titleBarRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  DrawTextW(hdc, L"X", -1, &closeButtonRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

void ShowCustomMenu(HWND hParentWnd) {
  WNDCLASS menuClass = {0};
  menuClass.lpfnWndProc = MenuWindowProc;
  menuClass.hInstance = GetModuleHandleA(NULL);
  menuClass.lpszClassName = "CustomMenuClass";
  RegisterClassA(&menuClass);

  RECT menuButtonRect = {4, 0, 34, TITLE_BAR_HEIGHT };
  POINT pt = { menuButtonRect.left, menuButtonRect.bottom };
  ClientToScreen(hParentWnd, &pt);

  int menuWidth = 150;
  int menuHeight = 100;

  hMenuWnd = CreateWindowExA(
    WS_EX_TOPMOST,
    "CustomMenuClass",
    NULL,
    WS_POPUP,
    pt.x,
    pt.y,
    menuWidth,
    menuHeight,
    hParentWnd,
    NULL,
    GetModuleHandleA(NULL),
    NULL
  );

  ShowWindow(hMenuWnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hMenuWnd);
}

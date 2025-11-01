// event_handler.c

#include "std/stdio.h"
#include "textedit/input_handling.h"

#include "wwindows//globals.h"
#include "wwindows.h"
#include "wwindows/drawing.h"

#include "wwindows/event_handler.h"



void HandlePaint(HWND hWnd) {
  PAINTSTRUCT ps;
  HANDLE hdc = BeginPaint(hWnd, &ps);

  SelectObject(hdc, editor.hFont);

  RECT clientRect;
  GetClientRect(hWnd, &clientRect);

  // HBRUSH debugBrush = CreateSolidBrush(RGB(255, 0, 0));
  // FrameRect(hdc, &clientRect, debugBrush);
  // DeleteObject(debugBrush);

  DrawTitleBar(hdc, clientRect);

  DrawTextEditor(hdc, &editor);

  // HBRUSH debuBrush = CreateSolidBrush(RGB(0, 0, 255));
  // RECT editorRect = { editor.x, editor.y, editor.x + editor.width, editor.y + editor.height };
  // FrameRect(hdc, &editorRect, debuBrush);

  EndPaint(hWnd, &ps);
}

BOOL HandleNCMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  int xPos = LOWORD(lParam);
  int yPos = HIWORD(lParam);

  POINT pt = { xPos, yPos};
  ScreenToClient(hWnd, &pt);
  RECT menuButtonRect = { 4, 0, 34, TITLE_BAR_HEIGHT};

  if (PtInRect(&menuButtonRect, pt)) {
    mouseHover = MH_MENU_BUTTON;
    if (!isTrackingMouseMenuButton) {
      TRACKMOUSEEVENT tme;
      tme.cbSize = sizeof(TRACKMOUSEEVENT);
      tme.dwFlags = TME_NONCLIENT | TME_HOVER | TME_LEAVE;
      tme.hwndTrack = hWnd;
      tme.dwHoverTime = HOVER_DEFAULT;
      TrackMouseEvent(&tme);
      isTrackingMouseMenuButton = TRUE;
    }

    InvalidateRect(hWnd, NULL, TRUE);
    UpdateWindow(hWnd);
  } else {
    mouseHover = NULL;
    if (hMenuWnd) {
      isMenuVisible = FALSE;
      DestroyWindow(hMenuWnd);
      isTrackingMouseMenuButton = FALSE;
      isTrackingMouseMenuContent = FALSE;
    }
  }
}

BOOL HandleNCLButtonDown(HWND hWnd, LPARAM lParam) {
  int xPos = LOWORD(lParam);
  int yPos = HIWORD(lParam);

  POINT pt = { xPos, yPos };
  ScreenToClient(hWnd, &pt);

  if ((pt.x > 4) && (pt.x < 34)) {

    return TRUE;
  }
  if (pt.x > windowWidth - 30) {
    DestroyWindow(hWnd);
    return TRUE;
  }
  return FALSE;
}

void HandleLButtonDown(HWND hWnd, LPARAM lParam) {
  int xPos = LOWORD(lParam);
  int yPos = HIWORD(lParam);

  SetCapture(hWnd);

  TextEditorHandleMouseDown(&editor, xPos, yPos);
  InvalidateRect(hWnd, NULL, TRUE);
}

void HandleLButtonUp(HWND hWnd) {
  ReleaseCapture();

  TextEditorHandleMouseUp(&editor);
  InvalidateRect(hWnd, NULL, TRUE);
}

void HandleMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  int xPos = LOWORD(lParam);
  int yPos = HIWORD(lParam);

  if (wParam & MK_LBUTTON) {
    TextEditorHandleMouseMove(&editor, xPos, yPos);
    InvalidateRect(hWnd, NULL, TRUE);
  }
}

void HandleTimer(HWND hWnd) {
  DWORD currentTime = GetTickCount();
  DWORD elapsedTime = currentTime - editor.lastCaretBlinkTime;

  if (elapsedTime >= TIMER_ELAPSED_TIME) {
    editor.caretBlinkInterval = CARET_BLINK_INTERVAL;
  }

  if (elapsedTime >= editor.caretBlinkInterval) {
    editor.caretVisible = !editor.caretVisible;
    editor.lastCaretBlinkTime = currentTime;
    InvalidateRect(hWnd, NULL, TRUE);
  }
}

void HandleNCMouseLeave(HWND hWnd) {
  mouseHover = NULL;
  if (isMenuVisible) {
    if (!IsCursorOverWindow(hMenuWnd)) {
      DestroyWindow(hMenuWnd);
      hMenuWnd = NULL;
      isMenuVisible = FALSE;
      isTrackingMouseMenuContent = FALSE;
    }
  }
  isTrackingMouseMenuButton = FALSE;
}

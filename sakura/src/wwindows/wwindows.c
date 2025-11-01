// wwindows.c

#include "std/stdio.h"
#include "textedit/input_handling.h"

#include "wwindows/globals.h"
#include "wwindows/event_handler.h"
#include "wwindows//drawing.h"

#include "wwindows.h"

LRESULT __stdcall MenuWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);

      HBRUSH hBrush = CreateSolidBrush(RGB(60, 60, 60));
      FillRect(hdc, &ps.rcPaint, hBrush);
      DeleteObject(hBrush);

      SetTextColor(hdc, RGB(255, 255, 255));
      SetBkMode(hdc, TRANSPARENT);

      RECT itemRect = { 10, 10, 140, 30 };
      DrawTextW(hdc, L"Menu Item 1", -1, &itemRect, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

      EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!isTrackingMouseMenuContent) {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        return 0;
      }
    }
    case WM_LBUTTONDOWN: {
      int xPos = LOWORD(lParam);
      int yPos = HIWORD(lParam);

      if (yPos >= 10 && yPos <= 30) {
        pprintf("Menu 1 clicked");
        DestroyWindow(hWnd);
        hMenuWnd = NULL;
        isMenuVisible = FALSE;
        isTrackingMouseMenuButton = FALSE;
        isTrackingMouseMenuContent = FALSE;
      }
      return 0;
    }
    case WM_MOUSELEAVE: {
      RECT clientRect;
      GetClientRect(hWnd, &clientRect);
      int xPos = LOWORD(lParam);
      int yPos = HIWORD(lParam);
      POINT pt = { xPos, yPos};
      RECT menuButtonRect = { clientRect.left + 4, clientRect.top, clientRect.left + 34, clientRect.top + TITLE_BAR_HEIGHT};
      if (!PtInRect(&menuButtonRect, pt)) {
        DestroyWindow(hWnd);
        hMenuWnd = NULL;
        isMenuVisible = FALSE;
        isTrackingMouseMenuButton = FALSE;
        isTrackingMouseMenuContent = FALSE;
      }
      return 0;
    }
    default:
      return DefWindowProcA(hWnd, message, wParam, lParam);
  }
}

LRESULT __stdcall WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
                    switch (message) {
                      case WM_CREATE: {
                        SetTimer(hWnd, 1, CARET_BLINK_INTERVAL, NULL);
                        return 0;
                      }
                      case WM_DESTROY: {
                        KillTimer(hWnd, 1);
                        PostQuitMessage(0);
                        return 0;
                      }
                      case WM_SIZE: {
                        RECT clientRect;
                        GetClientRect(hWnd, &clientRect);

                        windowWidth = clientRect.right - clientRect.left;
                        windowHeight = clientRect.bottom - clientRect.top;

                        editor.x = 10;
                        editor.y = TITLE_BAR_HEIGHT + 10;
                        editor.width = windowWidth - 20;
                        editor.height = windowHeight - TITLE_BAR_HEIGHT - 20;

                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                      }
                      case WM_PAINT: {
                        HandlePaint(hWnd);
                        return 0;
                      }
                      case WM_ERASEBKGND: {
                        return 1;
                      }
                      case WM_NCHITTEST: {
                        POINT pt = { LOWORD(lParam), HIWORD(lParam)};
                        ScreenToClient(hWnd, &pt);

                        if (pt.y < TITLE_BAR_HEIGHT) {
                          return HTCAPTION;
                        }
                        return DefWindowProcA(hWnd, message, wParam, lParam);
                      }
                      // case WM_NCCALCSIZE: {
                      //   if (wParam == TRUE) {
                      //     NCCALCSIZE_PARAMS* pncsp = (NCCALCSIZE_PARAMS*)lParam;
                      //   }
                      //   break;
                      // }
                      case WM_NCMOUSEMOVE: {
                        HandleNCMouseMove(hWnd, wParam, lParam);
                        return 0;
                      }
                      case WM_NCLBUTTONDOWN: {
                        BOOL is_handled = HandleNCLButtonDown(hWnd, lParam);
                        if (is_handled) {
                          return 0;
                        }
                        return DefWindowProcA(hWnd, message, wParam, lParam);
                      }
                      case WM_CHAR:
                        TextEditorHandleInput(&editor, message, wParam, lParam);
                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                      case WM_KEYDOWN:
                        TextEditorHandleInput(&editor, message, wParam, lParam);
                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                      case WM_KEYUP:
                        TextEditorHandleInput(&editor, message, wParam, lParam);
                        InvalidateRect(hWnd, NULL, TRUE);
                        return 0;
                      case WM_TIMER: {
                        HandleTimer(hWnd);
                        return 0;
                      }
                      case WM_MOUSEMOVE: {
                        HandleMouseMove(hWnd, wParam, lParam);
                        return 0;
                      }
                      case WM_LBUTTONDOWN: {
                        HandleLButtonDown(hWnd, lParam);
                        return 0;
                      }
                      case WM_LBUTTONUP: {
                        HandleLButtonUp(hWnd);
                        return 0;
                      }
                      case WM_NCMOUSEHOVER: {
                        if (!isMenuVisible) {
                          ShowCustomMenu(hWnd);
                          isMenuVisible = TRUE;
                        }
                        return 0;
                      }
                    case WM_MOUSEHOVER: {
                        return 0;
                      }
                    case WM_NCMOUSELEAVE: {
                        HandleNCMouseLeave(hWnd);
                        return 0;
                      }
                      default:
                        return DefWindowProcA(hWnd, message, wParam, lParam);
                    }
                  }

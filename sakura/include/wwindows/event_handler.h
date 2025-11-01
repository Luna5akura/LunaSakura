// event_handler.h

#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include "wwindows.h"

void HandlePaint(HWND hWnd);
BOOL HandleNCMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam);
BOOL HandleNCLButtonDown(HWND hWnd, LPARAM lParam);
void HandleLButtonDown(HWND hWnd, LPARAM lParam);
void HandleLButtonUp(HWND hWnd);
void HandleMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam);
void HandleTimer(HWND hWnd);
void HandleNCMouseLeave(HWND hWnd);

#endif

// main.c

#include "wwindows.h"
#include "textedit.h"

TextEditor editor;
HWND hMainWnd;

int main() {
  SetProcessDPIAware();

  HINSTANCE hinstance = GetModuleHandleA(NULL);

  WNDCLASS wc = {0};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hinstance;
  wc.hIcon = LoadIconA(NULL, (LPCSTR)(SIZE_T)32512); // IDI_APPLICATION = 32512
  wc.hCursor = LoadCursorA(NULL, (LPCSTR)(SIZE_T)32512); // IDC_ARROW = 32512
  wc.hbrBackground = (HANDLE)(SIZE_T)(COLOR_WINDOW + 1);
  wc.lpszClassName = "Sakura";

  if (!RegisterClassA(&wc)) {
    return -1;
  }

  hMainWnd = CreateWindowExA(
    0,
    wc.lpszClassName,
    "Luna Sakura",
    WS_POPUP,
    100,
    100,
    WINDOW_WIDTH,
    WINDOW_HEIGHT,
    NULL,
    NULL,
    hinstance,
    NULL
  );

  if (hMainWnd == NULL) {
    return -1;
  }

  InitializeEditor(&editor, hMainWnd);

  ShowWindow(hMainWnd, SW_SHOWDEFAULT);
  UpdateWindow(hMainWnd);

  MSG msg;
  while (GetMessageA(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }

  return 0;
}

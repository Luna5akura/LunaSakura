// globals.c

#include "wwindows.h"

#include "wwindows/globals.h"

int windowWidth = 500;
int windowHeight = 400;

HWND hMenuWnd = NULL;
BOOL isMenuVisible = FALSE;

BOOL isTrackingMouseMenuButton = FALSE;
BOOL isTrackingMouseMenuContent = FALSE;

mousehover mouseHover = NULL;

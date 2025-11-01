// wwindows.h

#ifndef WWINDOWS_H
#define WWINDOWS_H

#include "util/mem.h"

#define ZeroMemory(Destination, Length) mset((Destination), 0, (Length))

typedef unsigned short          wchar_t;

typedef void                    VOID;
typedef int                     INT;
typedef unsigned int            UINT;
typedef unsigned long           DWORD;
typedef int                     BOOL;
typedef unsigned char           BYTE;
typedef unsigned short          WORD;
typedef WORD                    ATOM;
typedef void*                   HANDLE;
typedef void*                   LPVOID;
typedef HANDLE                  HWND;
typedef HANDLE                  HINSTANCE;
typedef HANDLE                  HDC;
typedef HANDLE                  HBITMAP;
typedef HANDLE                  HGLOBAL;
typedef HANDLE                  HBRUSH;
typedef HANDLE                  HGDIOBJ;
typedef HANDLE                  HMENU;
typedef const char*             LPCSTR;
typedef const wchar_t*          LPCWSTR;
// typedef const LPCWSTR           LPCTSTR;
typedef long                    LONG;
typedef unsigned long           UINT_PTR;
typedef unsigned long           ULONG_PTR;
typedef unsigned long long      DWORD_PTR;
typedef ULONG_PTR               SIZE_T;
typedef unsigned int            UINT32_T;
typedef unsigned char           UINT8_T;
typedef long long int           INT64_T;
typedef LONG                    LRESULT;
typedef unsigned long long      WPARAM;
typedef long long               LPARAM;
typedef unsigned long           COLORREF;
typedef short                   SHORT;

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);



typedef union tagLARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    INT64_T QuadPart;
} LARGE_INTEGER;
typedef LARGE_INTEGER* PLARGE_INTEGER;

typedef struct tagBLENDFUNCTION {
    BYTE BlendOp;
    BYTE BlendFlags;
    BYTE SourceConstantAlpha;
    BYTE AlphaFormat;
} BLENDFUNCTION;

typedef  struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlane;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT;
typedef POINT* LPPOINT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
typedef RECT*  LPRECT;

typedef struct tagPAINTSTRUCT {
    HANDLE hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT;

typedef struct tagMEASUREITEMSTRUCT {
    UINT CtlType;
    UINT CtlID;
    UINT itemID;
    UINT itemWidth;
    UINT itemHeight;
    ULONG_PTR itemData;
} MEASUREITEMSTRUCT;

typedef struct tagDRAWITEMSTRUCT {
    UINT CtlType;
    UINT CtlID;
    UINT itemID;
    UINT itemAction;
    UINT itemState;
    HWND hwndItem;
    HDC hDC;
    RECT rcItem;
    ULONG_PTR itemData;
} DRAWITEMSTRUCT;

typedef struct tagTRACKMOUSEEVENT {
    DWORD cbSize;
    DWORD dwFlags;
    HWND hwndTrack;
    DWORD dwHoverTime;
} TRACKMOUSEEVENT;
typedef TRACKMOUSEEVENT *LPTRACKMOUSEEVENT;

typedef struct tagMSG {

    HWND    hwnd;
    UINT    message;
    WPARAM  wParam;
    LPARAM  lParam;
    DWORD   time;
    LONG    pt_x;
    LONG    pt_y;
} MSG;

typedef struct tagWNDCLASS {
    UINT        style;
    WNDPROC     lpfnWndProc;
    int         cbClsExtra;
    int         cbWndExtra;
    HINSTANCE   hInstance;
    HANDLE      hIcon;
    HANDLE      hCursor;
    HANDLE      hbrBackground;
    LPCSTR      lpszMenuName;
    LPCSTR      lpszClassName;
} WNDCLASS;

typedef struct tagWINDOWPOS {
    HWND hwnd;
    HWND hwndInsertAfter;
    int x;
    int y;
    int cx;
    int cy;
    UINT flag;
} WINDOWPOS, *PWINDOWPOS;

typedef struct tagNCCALCSIZE_PARAMS {
    RECT rgrc[3];
    PWINDOWPOS lppos;
} NCCALCSIZE_PARAMS, *LPNCCALCSIZE_PARAMS;



#define NULL 0
#define TRUE 1
#define FALSE 0

#define GMEM_MOVEABLE            0x0002

#define CF_TEXT                 1

#define COLOR_WINDOW            5
#define TRANSPARENT             1
#define SRCCOPY                 0x00CC0020
#define HTCAPTION               2
#define HOVER_DEFAULT           0xFFFFFFFF

#define WS_EX_TOPMOST           0x00000008L
// #define WS_EX_LAYERED           0x00080000
// #define WS_MINIMIZEBOX          0x00020000
// #define WS_SYSMENU              0x00080000
#define WS_BORDER               0x00800000L
#define WS_OVERLAPPEDWINDOW     0x00CF0000
#define WS_CLIPCHILDERN         0x02000000L
#define WS_VISIBLE              0x10000000
#define WS_POPUP                0x80000000L

#define SW_SHOWNOACTIVATE       4
#define SW_SHOWDEFAULT          10
#define CW_USEDEFAULT           ((int)0x80000000)
#define PM_REMOVE               0x0001
#define CS_HREDRAW              0x0002
#define CS_VREDRAW              0x0001
#define BI_RGB                  0L
#define DIB_RGB_COLORS          0
#define AC_SRC_OVER             0x00


#define WM_CREATE               0x0001
#define WM_DESTROY              0x0002
#define WM_SIZE                 0x0005
#define WM_KILLFOCUS            0x0008
#define WM_PAINT                0x000F
#define WM_ERASEBKGND           0x0014
#define WM_DRAWITEM             0x002B
#define WM_MEASUREITEM          0x002C
#define WM_NCCALCSIZE           0x0083
#define WM_NCHITTEST            0x0084
#define WM_NCMOUSEMOVE          0x00A0
#define WM_NCLBUTTONDOWN        0x00A1
#define WM_KEYDOWN              0x0100
#define WM_KEYUP                0x0101
#define WM_CHAR                 0x0102
#define WM_TIMER                0x0113
#define WM_MOUSEMOVE            0x0200
#define WM_LBUTTONDOWN          0x0201
#define WM_LBUTTONUP            0x0202
#define WM_IME_CHAR             0x0286
#define WM_NCMOUSEHOVER         0x02A0
#define WM_MOUSEHOVER           0x02A1
#define WM_NCMOUSELEAVE         0x02A2
#define WM_MOUSELEAVE           0x02A3

#define DT_LEFT                 0x0000
#define DT_CENTER               0x00000001
#define DT_VCENTER              0x00000004
#define DT_SINGLELINE           0x00000020

#define ODT_MENU                0x0001

#define ETO_OPAQUE              0x00000002

#define MB_OK                   0x00000000

// #define MF_STRING               0x00000000
#define MF_OWNERDRAW            0x00000100

#define TME_HOVER               0x00000001
#define TME_LEAVE               0x00000002
#define TME_NONCLIENT           0x00000010

#define TPM_LEFTALIGN           0x0000
#define TPM_TOPALIGN            0x0000

#define MK_LBUTTON              0x0001

#define VK_BACK                 0x08
#define VK_RETURN               0x0D
#define VK_SHIFT                0x10
#define VK_CONTROL              0x11
#define VK_LEFT                 0x25
#define VK_UP                   0x26
#define VK_RIGHT                0x27
#define VK_DOWN                 0x28
#define VK_DELETE               0x2E

#define LOWORD(lparam) ((WORD)((DWORD_PTR)(lparam) & 0xffff))
#define HIWORD(lparam) ((WORD)(((DWORD_PTR)(lparam) >> 16) & 0xffff))



extern BOOL      __stdcall SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod);

extern HGLOBAL   __stdcall GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
extern LPVOID    __stdcall GlobalLock(HGLOBAL hMem);
extern LPVOID    __stdcall GlobalUnlock(HGLOBAL hMem);

extern BOOL      __stdcall OpenClipboard(HWND hWndNewOwner);
extern BOOL      __stdcall EmptyClipboard(void);
extern HANDLE    __stdcall SetClipboardData(UINT uFormat, HANDLE hMem);
extern BOOL      __stdcall CloseClipboard(void);
extern HANDLE    __stdcall GetClipboardData(UINT uFormat);

extern HINSTANCE __stdcall GetModuleHandleA(LPCSTR lpModuleName);

extern ATOM      __stdcall RegisterClassA(const WNDCLASS* lpWndClass);
extern HWND      __stdcall CreateWindowExA(
                   DWORD      dwExStyle,
                   LPCSTR     lpClassName,
                   LPCSTR     lpWindowName,
                   DWORD      dwStyle,
                   int        X,
                   int        Y,
                   int        nWidth,
                   int        nHeight,
                   HWND       hWndParent,
                   HANDLE     hMenu,
                   HINSTANCE  hInstance,
                   void*      lpParam
                );
extern BOOL     __stdcall DestroyWindow(HWND hWnd);
extern BOOL     __stdcall ShowWindow(HWND hWnd, int nCmdShow);
extern BOOL     __stdcall UpdateWindow(HWND hWnd);
extern BOOL     __stdcall GetWindowRect(HWND hWnd, LPRECT lpRect);
extern BOOL     __stdcall InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
extern HMENU    __stdcall CreatePopupMenu();
extern BOOL     __stdcall AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem);
extern BOOL     __stdcall TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT *pcrRect);
extern BOOL     __stdcall DestroyMenu(HMENU hMenu);

extern BOOL     __stdcall TrackMouseEvent(LPTRACKMOUSEEVENT lpEventTrack);

extern BOOL     __stdcall ScreenToClient(HWND hWnd, LPPOINT lpPoint);
extern BOOL     __stdcall ClientToScreen(HWND hWnd, LPPOINT lpPoint);
extern BOOL     __stdcall GetClientRect(HWND hWnd, LPRECT lpRect);
extern BOOL     __stdcall FrameRect(HDC hDC, const RECT *lprc, HBRUSH hbr);
extern HGDIOBJ  __stdcall GetStockObject(int nPen);

extern BOOL     __stdcall GetMessageA(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
extern BOOL     __stdcall TranslateMessage(const MSG* lpMsg);
extern LRESULT  __stdcall DispatchMessageA(const MSG* lpMsg);

extern BOOL     __stdcall SetProcessDPIAware(void);
extern LRESULT  __stdcall DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern void     __stdcall PostQuitMessage(int nExitCode);

extern HANDLE   __stdcall LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
extern HANDLE   __stdcall LoadCursorA(HINSTANCE hinstance, LPCSTR lpCursorName);
extern BOOL     __stdcall GetCursorPos(LPPOINT lpPoint);

extern HANDLE   __stdcall BeginPaint(HWND hWnd, PAINTSTRUCT* lpPaint);
extern BOOL     __stdcall EndPaint(HWND hwnd, const PAINTSTRUCT* lpPaint);
extern BOOL     __stdcall FillRect(HANDLE hDC, const RECT* lprc, HANDLE hbr);
extern BOOL     __stdcall RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height);
extern BOOL     __stdcall BeginPath(HDC hdc);
extern BOOL     __stdcall EndPath(HDC hdc);
extern BOOL     __stdcall FillPath(HDC hdc);
extern int      __stdcall MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCSTR lpCaption, UINT uType);

extern HANDLE   __stdcall GetDC(HWND hWnd);
extern int      __stdcall ReleaseDC(HWND hWnd, HANDLE hDC);
extern BOOL     __stdcall DeleteDC(HDC hdc);
extern HDC      __stdcall CreateCompatibleDC(HDC hdc);
extern HBITMAP  __stdcall CreateCompatibleBitmap(HDC hdc, int width, int height);
extern HBITMAP  __stdcall CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, VOID **ppvBits, HANDLE hSection, DWORD offset);
extern BOOL     __stdcall BitBlt(HDC hdcDest, int xDest, int yDest, int width, int height, HDC hdcSrc, int xSrc, int ySrc, DWORD rop);
extern BOOL     __stdcall AlphaBlend(HDC hdcDest, int xDest, int yDest, int width, int height, HDC hdcSrc, int xSrc, int ySrc, int widthSrc, int heightSrc, BLENDFUNCTION blendFunction);


extern HWND     __stdcall SetCapture(HWND hWnd);
extern BOOL     __stdcall ReleaseCapture();

extern int      __stdcall DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format);
extern COLORREF __stdcall SetTextColor(HDC hdc, COLORREF cr);
extern int      __stdcall SetBkMode(HDC hdc, int mode);
extern COLORREF __stdcall SetBkColor(HDC hdc, COLORREF color);
extern BOOL     __stdcall ExtTextOutW(HDC hdc, int x, int y, UINT option, const RECT* lpRect, LPCSTR lpString, UINT nCount, const INT* lpDx);
extern BOOL     __stdcall TextOutA(HANDLE hdc, int x, int y, LPCSTR lpString, int c);
extern BOOL     __stdcall GetTextExtentPoint32A(HANDLE hdc, LPCSTR lpString, int c, SIZE* psizl);
extern BOOL     __stdcall PtInRect(const RECT *lprc, POINT pt);

extern UINT_PTR __stdcall SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, void* lpTimerFunc);
extern BOOL     __stdcall KillTimer(HWND hWnd, UINT_PTR uIDEvent);
extern DWORD    __stdcall GetTickCount();

extern HANDLE   __stdcall CreateSolidBrush(COLORREF color);
extern HANDLE   __stdcall CreateFontA(
                int nHeight, int nWidth, int nEscapement, int nOrientation,
                int fnWeight, DWORD fdwItalic, DWORD fdwUnderline, DWORD fdwStrikeOut,
                DWORD fdwCharSet, DWORD fdwOutputPrecision, DWORD fdwClipPrecision,
                DWORD fdwQuality, DWORD fdwPitchAndFamily, LPCSTR lpszFace
                );
extern HANDLE   __stdcall SelectObject(HANDLE hdc, HANDLE hgdiobj);
extern BOOL     __stdcall DeleteObject(HANDLE hObject);

extern SHORT            __stdcall GetKeyState(int nVirtKey);



LRESULT __stdcall MenuWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT  __stdcall WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#define TITLE_BAR_HEIGHT 30

#define WINDOW_WIDTH 500
#define WINDOW_HEIGHT 400

#define CARET_BLINK_INTERVAL 500
#define TIMER_ELAPSED_TIME 1000

typedef enum {
    MH_CLOSE_BUTTON,
    MH_MENU_BUTTON,
} mousehover;

#endif

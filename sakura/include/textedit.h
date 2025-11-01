// textedit.h

#include "wwindows.h"

#ifndef TEXTEDIT_H
#define TEXTEDIT_H

#define TEXT_EDITOR_INNER_MARGIN_X 8
#define TEXT_EDITOR_INNER_MARGIN_Y 4

typedef struct TextEditor {
  int x, y;
  int width, height;
  char text[10240];
  int textLength;
  int cursorPosition;
  int lineHeight;
  int visibleLines;
  int scrollOffset;
  long charWidth;
  BOOL caretVisible;
  int selectionStart;
  int selectionEnd;
  BOOL isSelecting;
  DWORD lastCaretBlinkTime;
  DWORD caretBlinkInterval;
  HANDLE hFont;
  BOOL isShiftPressed;
} TextEditor;


void DrawTextEditor(HANDLE hdc, TextEditor* editor);
void TextEditorHandleInput(TextEditor* editor, UINT message, WPARAM wParam, LPARAM lParam);
void InitializeEditor(TextEditor* editor, HWND hWnd);

#endif

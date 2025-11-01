// textedit.c

#include "util/mem.h"
#include "wwindows.h"

#include "textedit/text_operation.h"
#include "textedit/cursor_movement.h"

#include "textedit.h"

#define TAB_WIDTH 2

void DrawTextArea(TextEditor* editor, HANDLE hdcMem, int xOffset, int yOffset) {
  int lineNumber = 0;
  int charIndex = 0;
  int x = TEXT_EDITOR_INNER_MARGIN_X + xOffset;
  int y = TEXT_EDITOR_INNER_MARGIN_Y + yOffset;

  int selStart = editor->selectionStart;
  int selEnd = editor->selectionEnd;
  if (selStart > selEnd) {
    int temp = selStart;
    selStart = selEnd;
    selEnd = temp;
  }


  while (charIndex < editor->textLength && lineNumber - editor->scrollOffset < editor->visibleLines) {
    char lineBuffer[256];
    int lineLength = 0;

    while (charIndex < editor->textLength && editor->text[charIndex] != '\n' && lineLength < 255) {
      lineBuffer[lineLength] = editor->text[charIndex];

      int currentCharIndex = charIndex;
      if (currentCharIndex >= selStart && currentCharIndex < selEnd) {
        RECT charRect;
        charRect.left = x + lineLength * editor->charWidth;
        charRect.top = y + (lineNumber - editor->scrollOffset) * editor->lineHeight;
        charRect.right = charRect.left + editor->charWidth;
        charRect.bottom = charRect.top + editor->lineHeight;

        HANDLE hSelBrush = CreateSolidBrush(0x0000FF00);

        FillRect(hdcMem, &charRect, hSelBrush);
        DeleteObject(hSelBrush);
      }

      lineLength++;
      charIndex++;
    }

    if (charIndex < editor->textLength && editor->text[charIndex] == '\n') {
      charIndex++;
    }

    lineBuffer[lineLength] = '\0';

    if (lineNumber >= editor->scrollOffset) {
      TextOutA(hdcMem, x, y + (lineNumber - editor->scrollOffset) * editor->lineHeight, lineBuffer, lineLength);
    }

    lineNumber++;
  }
}

void DrawCursor(TextEditor* editor, HANDLE hdcMem, int xOffset, int yOffset) {
  int cursorLine = 0;
  int cursorColumn = 0;
  int charIndex = 0;
  int x = TEXT_EDITOR_INNER_MARGIN_X + xOffset;
  int y = TEXT_EDITOR_INNER_MARGIN_Y + yOffset;

  while (charIndex < editor->cursorPosition) {
    if (editor->text[charIndex] == '\n') {
      cursorLine++;
      cursorColumn = 0;
      if (cursorLine - editor->scrollOffset >= editor->visibleLines) {
        editor->scrollOffset = cursorLine - editor->visibleLines + 1;
      }
    } else {
      cursorColumn++;
    }
    charIndex++;
  }

  if (cursorLine >= editor->scrollOffset && cursorLine - editor->scrollOffset < editor->visibleLines) {
    SIZE size;
    GetTextExtentPoint32A(hdcMem, "A", 1, &size);

    int cursorX = x + cursorColumn * size.cx;
    int cursorY = y + (cursorLine - editor->scrollOffset) * editor->lineHeight;

    RECT cursorRect = { cursorX, cursorY, cursorX + 2, cursorY + editor->lineHeight };
    HANDLE hCursorBrush = CreateSolidBrush(0x00000000);
    if (editor->caretVisible && cursorLine >= editor->scrollOffset && cursorLine - editor->scrollOffset < editor->visibleLines) {
      FillRect(hdcMem, &cursorRect, hCursorBrush);
    }
    DeleteObject(hCursorBrush);
  }
}

void DrawTextEditor(HANDLE hdc, TextEditor* editor) {
  HANDLE hdcMem = CreateCompatibleDC(hdc);
  HANDLE hBitmap = CreateCompatibleBitmap(hdc, editor->width, editor->height);
  HANDLE hOldBitmap = SelectObject(hdcMem, hBitmap);

  RECT rect = { 0, 0, editor->width, editor->height };
  HANDLE hBrush = CreateSolidBrush(0x00FFFFFF);
  FillRect(hdcMem, &rect, hBrush);
  DeleteObject(hBrush);

  HANDLE hOldFont = SelectObject(hdcMem, editor->hFont);
  int oldBkMode = SetBkMode(hdcMem, TRANSPARENT);

  DrawTextArea(editor, hdcMem, 0, 0);
  DrawCursor(editor, hdcMem, 0, 0);

  SetBkMode(hdcMem, oldBkMode);
  SelectObject(hdcMem, hOldFont);

  BitBlt(hdc, editor->x, editor->y, editor->width, editor->height, hdcMem, 0, 0, SRCCOPY);

  SelectObject(hdcMem, hOldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hdcMem);

  // HBRUSH debugBrush = CreateSolidBrush(RGB(0, 255, 0));
  // RECT debugRect = { 0, 0, editor->width, editor->height };
  // FrameRect(hdcMem, &debugRect, debugBrush);
}

void TextEditorHandleInput(TextEditor* editor, UINT message, WPARAM wParam, LPARAM lParam) {
  if (GetKeyState(VK_CONTROL) & 0x8000) {
    if (message == WM_KEYDOWN) {
      switch (wParam) {
        case 'C': {
          TextEditorCopy(editor);
          break;
        }
        case 'V': {
          TextEditorPaste(editor);
          break;
        }
        case 'X': {
          TextEditorCut(editor);
          break;
        }
        case 'A': {
          TextEditorSelectAll(editor);
          break;
        }
      }
    }
  } else if (message == WM_CHAR) {
    char ch = (char)wParam;
    if (editor->selectionStart != editor->selectionEnd) {
      int start = (editor->selectionStart<editor->selectionEnd) ? editor->selectionStart : editor->selectionEnd;
      int end = (editor->selectionStart<editor->selectionEnd) ? editor->selectionEnd : editor->selectionStart;
      int len = end - start;

      mmemmove(&editor->text[start], &editor->text[end], editor->textLength - end + 1);
      editor->textLength -= len;
      editor->cursorPosition = start;
      editor->selectionStart = editor->selectionEnd = start;
    }



    if (ch == '\b') {

    } else if (ch == '\r' || ch == '\n') {
      if (editor->textLength < sizeof(editor->text) - 1) {
        mmemmove(&editor->text[editor->cursorPosition + 1], &editor->text[editor->cursorPosition], editor->textLength - editor->cursorPosition + 1);
        editor->text[editor->cursorPosition] = '\n';
        editor->cursorPosition++;
        editor->textLength++;
      }
    } else if (ch == '\t') {
      for (int i = 0; i < TAB_WIDTH; i++) {
        if (editor->textLength < sizeof(editor->text) - 1) {
          mmemmove(&editor->text[editor->cursorPosition + 1],
            &editor->text[editor->cursorPosition],
            editor->textLength - editor->cursorPosition + 1);
          editor->text[editor->cursorPosition] = ' ';
          editor->cursorPosition++;
          editor->textLength++;
        }
      }
    }
    else {
      if (editor->textLength < sizeof(editor->text) - 1) {
        mmemmove(&editor->text[editor->cursorPosition + 1], &editor->text[editor->cursorPosition], editor->textLength - editor->cursorPosition + 1);
        editor->text[editor->cursorPosition] = ch;
        editor->cursorPosition++;
        editor->textLength++;
      }
    }
  } else if (message == WM_KEYDOWN) {
    switch (wParam) {
      case VK_BACK: {
        HandleVKBack(editor);
        break;
      }
      case VK_SHIFT: {
        editor->selectionStart = editor->cursorPosition;
        editor->selectionEnd = editor->cursorPosition;
        editor->isSelecting = TRUE;
        editor->isShiftPressed = TRUE;
        break;
      }
      case VK_LEFT: {
        HandleVKLeft(editor);
        break;
      }
      case VK_RIGHT: {
        HandleVKRight(editor);
        break;
      }
      case VK_UP: {
        HandleVKUp(editor);
        break;
      }
      case VK_DOWN: {
        HandleVKDown(editor);
        break;
      }
      case VK_DELETE: {
        HandleVKDelete(editor);
        break;
      }
    }
  } else if (message == WM_KEYUP) {
    switch (wParam) {
      case VK_SHIFT: {
        editor->isSelecting = FALSE;
        editor->isShiftPressed = FALSE;
        break;
      }
    }
  }
  int cursorLine = 0;
  int charIndex = 0;
  while (charIndex < editor->cursorPosition) {
    if (editor->text[charIndex] == '\n') {
      cursorLine++;
    }
    charIndex++;
  }

  if (cursorLine < editor->scrollOffset) {
    editor->scrollOffset = cursorLine;
  } else if (cursorLine >= editor->scrollOffset + editor->visibleLines) {
    editor->scrollOffset = cursorLine - editor->visibleLines + 1;
  }
  editor->caretVisible = TRUE;

  if (!editor->isShiftPressed) {
    editor->isSelecting = FALSE;
  }
  editor->lastCaretBlinkTime = GetTickCount();
}

void InitializeEditor(TextEditor* editor, HWND hWnd) {
  editor->x = 0;
  editor->y = TITLE_BAR_HEIGHT;
  editor->width = WINDOW_WIDTH;
  editor->height = WINDOW_HEIGHT;
  editor->textLength = 0;
  editor->cursorPosition = 0;
  editor->lineHeight = 16;
  editor->visibleLines = (editor->height - TITLE_BAR_HEIGHT) / editor->lineHeight;
  editor->scrollOffset = 0;
  editor->caretVisible = TRUE;
  editor->selectionStart = 0;
  editor->selectionEnd = 0;
  editor->isSelecting = FALSE;
  editor->caretVisible = TRUE;
  editor->lastCaretBlinkTime = GetTickCount();
  editor->caretBlinkInterval = CARET_BLINK_INTERVAL;
  editor->hFont = CreateFontA(
    26, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, "JetBrains Mono"
  );

  HANDLE hdc = GetDC(hWnd);
  SelectObject(hdc, editor->hFont);
  // HANDLE hOldFont = SelectObject(hdc, editor->hFont);

  SIZE size;
  GetTextExtentPoint32A(hdc, "A", 1, &size);
  editor->charWidth = size.cx;
  editor->lineHeight = size.cy;

  // SelectObject(hdc, hOldFont);
  ReleaseDC(hWnd, hdc);
}

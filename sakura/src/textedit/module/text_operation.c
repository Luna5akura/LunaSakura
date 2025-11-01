// text_operation.c

#include "std/math.h"
#include "util/mem.h"
#include "util/string_util.h"

#include "textedit.h"

#include "textedit/text_operation.h"


void TextEditorCopy(TextEditor* editor) {
  if (editor->selectionStart != editor->selectionEnd) {
    int start = min(editor->selectionStart,editor->selectionEnd);
    int end = max(editor->selectionStart, editor->selectionEnd);
    int len = end - start;

    HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if (hMem) {
      char* buffer = (char*)GlobalLock(hMem);
      if (buffer) {
        mcopy(buffer, &editor->text[start], len);
        buffer[len] = '\0';
        GlobalUnlock(hMem);

        if (OpenClipboard(NULL)) {
          EmptyClipboard();
          SetClipboardData(CF_TEXT, hMem);
          CloseClipboard();
        }
      }
    }
  }
}

void TextEditorPaste(TextEditor* editor) {
  if (OpenClipboard(NULL)) {
    HANDLE hMem = GetClipboardData(CF_TEXT);
    if (hMem) {
      char* buffer = (char*)GlobalLock(hMem);
      if (buffer) {
        int len = slen(buffer);
        if (editor->selectionStart != editor->selectionEnd) {
          int start = min(editor->selectionStart, editor->selectionEnd);
          int end = max(editor->selectionStart, editor->selectionEnd);
          int delLen = end - start;
          memmove(&editor->text[start], &editor->text[end], editor->textLength - end + 1);
          editor->textLength -=delLen;
          editor->cursorPosition = start;
          editor->isSelecting = FALSE;
          editor->selectionStart = 0;
          editor->selectionEnd = 0;
        }
        memmove(&editor->text[editor->cursorPosition + len], &editor->text[editor->cursorPosition], editor->textLength - editor->cursorPosition + 1);
        mcopy(&editor->text[editor->cursorPosition], buffer, len);
        editor->textLength += len;
        editor->cursorPosition += len;
        // editor->cursorPosition = editor->selectionEnd = editor->cursorPosition;
        GlobalUnlock(hMem);
      }
    }
    CloseClipboard();
  }
}

void TextEditorCut(TextEditor* editor) {
  TextEditorCopy(editor);
  if (editor->selectionStart != editor->selectionEnd) {
    int start = min(editor->selectionStart, editor->selectionEnd);
    int end = max(editor->selectionStart, editor->selectionEnd);
    int len = end - start;
    memmove(&editor->text[start], &editor->text[end], editor->textLength - end + 1);
    editor->textLength -= len;
    editor->cursorPosition = start;
    editor->selectionStart = editor->selectionEnd = start;
  }
}

void TextEditorSelectAll(TextEditor* editor) {
  editor->selectionStart = 0;
  editor->selectionEnd = editor->textLength;
  editor->cursorPosition = editor->textLength;
}


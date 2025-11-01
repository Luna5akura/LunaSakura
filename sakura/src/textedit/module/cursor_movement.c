// cursor_movement.c

#include "util/mem.h"

#include "textedit.h"

#include "textedit/cursor_movement.h"

int GetCursorPositionFromPoint(TextEditor* editor, int xPos, int yPos) {
  int relativeX = xPos - editor->x - 2;
  int relativeY = yPos - editor->y - 2;

  int clickedLine = relativeY / editor->lineHeight + editor->scrollOffset;
  int clickedColumn = relativeX / editor->charWidth;

  int lineNumber = 0;
  int charIndex = 0;
  int lineStartIndex = 0;

  while (charIndex < editor->textLength) {
    if (lineNumber == clickedLine) {
      int lineEndIndex = charIndex;
      while (lineEndIndex < editor->textLength && editor->text[lineEndIndex] != '\n') {
        lineEndIndex++;
      }

      int lineLength = lineEndIndex - lineStartIndex;
      int targetColumn = (clickedColumn < lineLength) ? clickedColumn : lineLength;

      return lineStartIndex + targetColumn;
    }

    if (editor->text[charIndex] == '\n') {
      lineNumber+=1;
      lineStartIndex = charIndex + 1;
    }
    charIndex++;
  }

  return editor->textLength;
}

void HandleVKBack(TextEditor* editor) {
  if (editor->selectionStart != editor->selectionEnd) {
    int start = (editor->selectionStart<editor->selectionEnd) ? editor->selectionStart : editor->selectionEnd;
    int end = (editor->selectionStart<editor->selectionEnd) ? editor->selectionEnd : editor->selectionStart;
    int len = end - start;

    mmemmove(&editor->text[start], &editor->text[end], editor->textLength - end + 1);
    editor->textLength -= len;
    editor->cursorPosition = start;
    editor->selectionStart = editor->selectionEnd = start;
  } else if (editor->cursorPosition > 0) {
    mmemmove(&editor->text[editor->cursorPosition - 1], &editor->text[editor->cursorPosition], editor->textLength - editor->cursorPosition + 1);
    editor->cursorPosition--;
    editor->textLength--;
  }
}

void HandleVKLeft(TextEditor* editor) {
  if (editor->cursorPosition > 0) {
    if (editor->isShiftPressed) {
      editor->selectionEnd--;
    } else {
      editor->selectionEnd = 0;
      editor->selectionStart = 0;
    }
    editor->cursorPosition--;
  }
}

void HandleVKRight(TextEditor* editor) {
  if (editor->cursorPosition < editor->textLength) {
    if (editor->isShiftPressed) {
      editor->selectionEnd++;
    } else {
      editor->selectionEnd = 0;
      editor->selectionStart = 0;
    }
    editor->cursorPosition++;
  }
}

void HandleVKUp(TextEditor* editor) {
  if (editor->cursorPosition == 0) {
    return;
  }

  int lineStart = editor->cursorPosition;
  while (lineStart > 0 && editor->text[lineStart - 1] != '\n') {
    lineStart--;
  }

  if (lineStart == 0) {
    if (editor->isShiftPressed) {
      editor->selectionEnd = 0;
    }
    editor->cursorPosition = 0;
    return;
  }

  int prevLineEnd = lineStart - 1;
  int prevLineStart = prevLineEnd;
  while (prevLineStart > 0 && editor->text[prevLineStart - 1] != '\n') {
    prevLineStart--;
  }

  int column = editor->cursorPosition - lineStart;

  int prevLineLength = prevLineEnd - prevLineStart;
  int newCursorPos = prevLineStart + (column < prevLineLength ? column : prevLineLength);

  if (editor->isShiftPressed) {
    editor->selectionEnd = newCursorPos;
  } else {
    editor->isSelecting = FALSE;
    editor->selectionStart = 0;
    editor->selectionEnd = 0;
  }
  editor->cursorPosition = newCursorPos;
  editor->caretVisible = TRUE;
  editor->lastCaretBlinkTime = GetTickCount();

}

void HandleVKDown(TextEditor* editor) {
  int lineEnd = editor->cursorPosition;
  while (lineEnd < editor->textLength && editor->text[lineEnd] != '\n') {
    lineEnd++;
  }

  if (lineEnd >= editor->textLength) {
    return;
  }

  lineEnd++;

  int nextLineEnd = lineEnd;
  while (nextLineEnd < editor->textLength && editor->text[nextLineEnd] != '\n') {
    nextLineEnd++;
  }

  int lineStart = editor->cursorPosition;
  while (lineStart > 0 && editor->text[lineStart - 1] != '\n') {
    lineStart--;
  }
  int column = editor->cursorPosition - lineStart;

  int nextLineLength = nextLineEnd - lineEnd;
  int newCursorPos = lineEnd + (column < nextLineLength ? column : nextLineLength);

  if (editor->isShiftPressed) {
    editor->selectionEnd = newCursorPos;
  } else {
    editor->isSelecting = FALSE;
    editor->selectionStart = 0;
    editor->selectionEnd = 0;
  }
  editor->cursorPosition = newCursorPos;
  editor->caretVisible = TRUE;
  editor->lastCaretBlinkTime = GetTickCount();
}

void HandleVKDelete(TextEditor* editor) {
  if (editor->selectionStart != editor->selectionEnd) {
    int start = (editor->selectionStart<editor->selectionEnd) ? editor->selectionStart : editor->selectionEnd;
    int end = (editor->selectionStart<editor->selectionEnd) ? editor->selectionEnd : editor->selectionStart;
    int len = end - start;

    mmemmove(&editor->text[start], &editor->text[end], editor->textLength - end + 1);
    editor->textLength -= len;
    editor->cursorPosition = start;
    editor->selectionStart = editor->selectionEnd = start;
  } else if (editor->cursorPosition > 0) {
    mmemmove(&editor->text[editor->cursorPosition - 1], &editor->text[editor->cursorPosition], editor->textLength - editor->cursorPosition + 1);
    editor->cursorPosition--;
    editor->textLength--;
  }
}

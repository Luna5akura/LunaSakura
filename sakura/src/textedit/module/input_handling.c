// input_handling.c

#include "textedit/cursor_movement.h"
#include "textedit.h"

#include "textedit/input_handling.h"

void TextEditorHandleMouseDown(TextEditor* editor, int xPos, int yPos) {
  int newCursorPos = GetCursorPositionFromPoint(editor, xPos, yPos);
  editor->cursorPosition = newCursorPos;
  editor->selectionStart = newCursorPos;
  editor->selectionEnd = newCursorPos;
  editor->isSelecting = TRUE;
}

void TextEditorHandleMouseMove(TextEditor* editor, int xPos, int yPos) {
  if (editor->isSelecting) {
    int newCursorPos = GetCursorPositionFromPoint(editor, xPos, yPos);
    editor->cursorPosition = newCursorPos;
    editor->selectionEnd =newCursorPos;
  }
}

void TextEditorHandleMouseUp(TextEditor* editor) {
  editor->isSelecting = FALSE;
}

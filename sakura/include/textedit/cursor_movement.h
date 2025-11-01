// cursor_movement.h

#ifndef CURSOR_MOVEMENT_H
#define CURSOR_MOVEMENT_H

#include "textedit.h"

int GetCursorPositionFromPoint(TextEditor* editor, int xPos, int yPos);
void HandleVKBack(TextEditor* editor);
void HandleVKLeft(TextEditor* editor);
void HandleVKRight(TextEditor* editor);
void HandleVKUp(TextEditor* editor);
void HandleVKDown(TextEditor* editor);
void HandleVKDelete(TextEditor* editor);

#endif

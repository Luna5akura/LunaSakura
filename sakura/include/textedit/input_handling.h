// input_handling.h

#ifndef INPUT_HANDLING_H
#define INPUT_HANDLING_H

#include "textedit.h"

void TextEditorHandleMouseDown(TextEditor* editor, int xPos, int yPos);
void TextEditorHandleMouseMove(TextEditor* editor, int xPos, int yPos);
void TextEditorHandleMouseUp(TextEditor* editor);

#endif

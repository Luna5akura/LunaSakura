// text_operation.h

#ifndef TEXT_OPERATION_H
#define TEXT_OPERATION_H

#include "textedit.h"

void TextEditorCopy(TextEditor* editor);
void TextEditorPaste(TextEditor* editor);
void TextEditorCut(TextEditor* editor);
void TextEditorSelectAll(TextEditor* editor);

#endif

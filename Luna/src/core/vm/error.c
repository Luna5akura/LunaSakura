// src/core/vm/error.c

#include "error.h"
#include "vm.h"  // Added for full VM definition
#include <stdlib.h>

bool runtimeError(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Runtime Error: ");
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    for (i32 i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
     
        size_t instruction = frame->ip - function->chunk.code - 1;
        i32 line = getLine(&function->chunk, (int)instruction);
     
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    if (vm->handlerCount == 0) {
        resetStack(vm);
        return false; // Stop VM
    }
    // Exception handling logic
    Handler handler = vm->handlers[--vm->handlerCount];
    for (int i = vm->frameCount - 1; i > handler.frameIndex; i--) {
        CallFrame* unwindFrame = &vm->frames[i];
        closeUpvalues(vm, unwindFrame->slots);
    }
    vm->frameCount = handler.frameIndex + 1;
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    frame->ip = handler.handlerIp;
    vm->stackTop = handler.tryStackTop;
    closeUpvalues(vm, handler.tryStackTop);
    return true; // Continue VM
}
// src/core/compiler/compiler_emit.c

#include "compiler_internal.h"
#include "memory.h"

void emitByte(u8 byte) {
    writeChunk(compilingVM, currentChunk(), byte, parser.previous.line);
}

void emitBytes(u8 b1, u8 b2) {
    emitByte(b1);
    emitByte(b2);
}

void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0); // 返回 this
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

u32 makeConstant(Value value) {
    i32 constant = addConstant(compilingVM, currentChunk(), value);
    if (constant > 16777215) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (u32)constant;
}

void emitConstant(Value value) {
    u32 constant = makeConstant(value);
    if (constant <= UINT8_MAX) {
        emitBytes(OP_CONSTANT, (u8)constant);
    } else {
        emitByte(OP_CONSTANT_LONG);
        emitByte((u8)(constant & 0xFF));
        emitByte((u8)((constant >> 8) & 0xFF));
        emitByte((u8)((constant >> 16) & 0xFF));
    }
}

i32 emitJump(u8 instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

void patchJump(i32 offset) {
    i32 jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

void emitLoop(i32 loopStart) {
    emitByte(OP_LOOP);
    i32 offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
  
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}
// src/vm/chunk.h

#pragma once
#include "value.h"

// --- Instruction Set ---
typedef enum {
    OP_CONSTANT,
    OP_CONSTANT_LONG,
    
    // [新增] 常用字面量优化指令
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    
    OP_POP,
    
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_DEFINE_GLOBAL,
    
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    
    OP_NOT,
    OP_NEGATE,
    
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_RETURN,
} OpCode;

// --- Line Info ---
typedef struct {
    i32 line;
    u32 count;
} LineStart;

typedef struct {
    u32 count;
    u32 capacity;
    LineStart* lines;
} LineInfo;

// --- Bytecode Chunk ---
typedef struct {
    u32 count;
    u32 capacity;
    u8* code;
    ValueArray constants;
  
    i32 bufferedLine;
    u32 bufferedCount;
    LineInfo lineInfo;
} Chunk;

// [新增] 前置声明 VM
typedef struct VM VM;

void initChunk(Chunk* chunk);
void freeChunk(VM* vm, Chunk* chunk);
void growChunkCode(VM* vm, Chunk* chunk);
void flushLineBuffer(VM* vm, Chunk* chunk, i32 newLine);
i32 addConstant(VM* vm, Chunk* chunk, Value value);

// --- Hot Path: Bytecode Writer ---
static INLINE void writeChunk(VM* vm, Chunk* chunk, u8 byte, i32 line) {
    if (UNLIKELY(chunk->count == chunk->capacity)) {
        growChunkCode(vm, chunk);
    }
    chunk->code[chunk->count++] = byte;
    if (LIKELY(chunk->lineInfo.count > 0 && line == chunk->bufferedLine)) {
        chunk->bufferedCount++;
    } else {
        flushLineBuffer(vm, chunk, line);
    }
}

static INLINE void writeChunkByte(VM* vm, Chunk* chunk, u8 byte) {
    writeChunk(vm, chunk, byte, chunk->bufferedLine);
}

i32 getLine(Chunk* chunk, i32 instructionOffset);

// --- Debugging ---
i32 disassembleInstruction(Chunk* chunk, i32 offset);
void disassembleChunk(Chunk* chunk, const char* name);
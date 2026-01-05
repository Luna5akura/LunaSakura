// src/vm/chunk.h

#pragma once
#include "value.h"

// [新增] 前置声明 VM
typedef struct VM VM;

// --- Instruction Set ---
typedef enum {
    OP_CONSTANT,
    OP_CONSTANT_LONG,
    OP_NEGATE,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_LESS,
    OP_POP,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_PRINT,
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

void initChunk(Chunk* chunk);

// [修改] 增加 VM* vm 参数
void freeChunk(VM* vm, Chunk* chunk);

// [修改] 增加 VM* vm 参数
void growChunkCode(VM* vm, Chunk* chunk);

// [修改] 增加 VM* vm 参数
void flushLineBuffer(VM* vm, Chunk* chunk, i32 newLine);

// [修改] 增加 VM* vm 参数
i32 addConstant(VM* vm, Chunk* chunk, Value value);

// --- Hot Path: Bytecode Writer ---
// [修改] 增加 VM* vm 参数
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

// [修改] 增加 VM* vm 参数
static INLINE void writeChunkByte(VM* vm, Chunk* chunk, u8 byte) {
    writeChunk(vm, chunk, byte, chunk->bufferedLine);
}

// --- Debugging ---
i32 disassembleInstruction(Chunk* chunk, i32 offset);
void disassembleChunk(Chunk* chunk, const char* name);
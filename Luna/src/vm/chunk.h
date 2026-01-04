// src/vm/chunk.h

#pragma once
#include "common.h"
#include "value.h"

// --- Instruction Set ---
typedef enum {
    OP_CONSTANT,
    OP_CONSTANT_LONG, // 预留：处理 >256 个常量
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

// --- Line Info (RLE) ---
typedef struct {
    int line;
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
    u8* code; // Hot Data
    ValueArray constants; // Hot Data
   
    // --- RLE Optimization Buffer ---
    // 将行号写入逻辑内联化，避免频繁调用 addLine
    int bufferedLine;
    u32 bufferedCount;
    LineInfo lineInfo; // Cold Data (Separated for cache locality)
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
// 慢路径：扩容
void growChunkCode(Chunk* chunk);
// 慢路径：刷新行号缓冲
void flushLineBuffer(Chunk* chunk, int newLine);
int addConstant(Chunk* chunk, Value value);

// --- Hot Path: Bytecode Writer ---
static INLINE void writeChunk(Chunk* chunk, u8 byte, int line) {
    // 1. 扩容检查
    if (UNLIKELY(chunk->count == chunk->capacity)) {
        growChunkCode(chunk);
    }
   
    // 2. 写入指令
    chunk->code[chunk->count++] = byte;
    // 3. 极速行号记录 (寄存器级操作)
    if (LIKELY(chunk->lineInfo.count > 0 && line == chunk->bufferedLine)) {
        chunk->bufferedCount++;
    } else {
        flushLineBuffer(chunk, line);
    }
}
// 辅助：写入操作数（沿用当前行号）
static INLINE void writeChunkByte(Chunk* chunk, u8 byte) {
    writeChunk(chunk, byte, chunk->bufferedLine);
}

// --- Debugging ---
int disassembleInstruction(Chunk* chunk, int offset);
void disassembleChunk(Chunk* chunk, const char* name);
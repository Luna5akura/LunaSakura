// src/core/chunk.h

#pragma once
#include "value.h"
// --- Instruction Set ---
typedef enum {
    OP_CONSTANT,
    OP_CONSTANT_LONG,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
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
    OP_CALL_KW,
    OP_CHECK_DEFAULT,// [新增] 检查参数是否为 UNDEFINED (slot, jump_offset)
    OP_ITER_INIT, // 初始化迭代器: [iterable] -> [iterable, iterator_index]
    OP_ITER_NEXT, // 迭代步进: [iterable, iterator_index] -> [iterable, new_index, item] (如果结束则跳转)
    OP_BUILD_LIST,
    OP_BUILD_DICT,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_INVOKE,
    OP_INVOKE_KW,
    OP_SUPER_INVOKE,
    OP_SUPER_INVOKE_KW,
    OP_TRY,
    OP_POP_HANDLER
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
    // --- Hot Fields (频繁访问) ---
    u8* code;       // 指向字节码数组起始
    u8* codeTop;    // [新增] 指向下一个可写入位置 (代替 count)
    u8* codeLimit;  // [新增] 指向数组结束位置 (代替 capacity)
    
    ValueArray constants;
    
    // --- Cold / Compile-time Fields ---
    LineInfo lineInfo;
    i32 bufferedLine;
    u32 bufferedCount;
} Chunk;
typedef struct VM VM;
void initChunk(Chunk* chunk);
void freeChunk(VM* vm, Chunk* chunk);
void growChunkCode(VM* vm, Chunk* chunk); // Cold path
void flushLineBuffer(VM* vm, Chunk* chunk, i32 newLine);
i32 addConstant(VM* vm, Chunk* chunk, Value value);

// --- Hot Path: Bytecode Writer ---
static INLINE void writeChunk(VM* vm, Chunk* chunk, u8 byte, i32 line) {
    // 现在的检查变成了指针比较，少了一次偏移量计算
    if (UNLIKELY(chunk->codeTop >= chunk->codeLimit)) {
        growChunkCode(vm, chunk); // 内部会更新 code, codeTop, codeLimit
    }
    
    *chunk->codeTop = byte;
    chunk->codeTop++;
    
    if (LIKELY(line == chunk->bufferedLine)) {
        chunk->bufferedCount++;
    } else {
        flushLineBuffer(vm, chunk, line);
    }
}
static INLINE u32 getChunkCount(Chunk* chunk) {
    return (u32)(chunk->codeTop - chunk->code);
}
static INLINE void writeChunkByte(VM* vm, Chunk* chunk, u8 byte) {
    writeChunk(vm, chunk, byte, chunk->bufferedLine);
}
i32 getLine(Chunk* chunk, i32 instructionOffset);
i32 disassembleInstruction(Chunk* chunk, i32 offset);
void disassembleChunk(Chunk* chunk, const char* name);
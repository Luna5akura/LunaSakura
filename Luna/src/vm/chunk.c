// src/vm/chunk.c

#include <stdlib.h>
#include <stdio.h>
#include "chunk.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
    free(chunk->code);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = (oldCapacity < 8) ? 8 : oldCapacity * 2;
        chunk->code = (uint8_t*)realloc(chunk->code, sizeof(uint8_t) * chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    // 返回刚刚写入的那个常量的索引
    return chunk->constants.count - 1;
}

// === 下面是调试工具，非常重要 ===

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    // 指令格式: [OP_CONSTANT] [index]
    // index 存储在下一个字节
    uint8_t constantIndex = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constantIndex);
    printValue(chunk->constants.values[constantIndex]);
    printf("'\n");
    return offset + 2;
}

// 去掉 static，让 VM 可以调用它打印调试信息
int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    
    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);

        case OP_ADD:      return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT: return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY: return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:   return simpleInstruction("OP_DIVIDE", offset);
        case OP_DEFINE_GLOBAL: return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL:    return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:    return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_POP:           return simpleInstruction("OP_POP", offset);
        case OP_PRINT: return simpleInstruction("OP_PRINT", offset);

        case OP_CALL: return constantInstruction("OP_CALL", chunk, offset); // 这里其实可以用 simpleInstruction 变体打印 argCount，暂时用 constantInstruction 凑合或者自己写个 byteInstruction
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

// 原来的 disassembleChunk 只要循环调用上面这个函数即可
void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}
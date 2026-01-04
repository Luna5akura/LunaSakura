// src/vm/chunk.c

#include <stdio.h>
#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "vm.h"  // 新增：为 extern VM vm

extern VM vm;  // 声明全局 vm

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    initValueArray(&chunk->constants);
   
    // RLE Line Info Initialization
    chunk->lineInfo.count = 0;
    chunk->lineInfo.capacity = 0;
    chunk->lineInfo.lines = NULL;
    // 1. 新增：初始化 RLE 缓冲区
    // -1 表示当前没有正在缓冲的行
    chunk->bufferedLine = -1;
    chunk->bufferedCount = 0;
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(&vm, u8, chunk->code, chunk->capacity);  // 传入 &vm
    FREE_ARRAY(&vm, LineStart, chunk->lineInfo.lines, chunk->lineInfo.capacity);  // 传入 &vm
   
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

// === Cold Path: Code Expansion ===
// 2. 优化：跨平台 noinline
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void growChunkCode(Chunk* chunk) {
    u32 oldCapacity = chunk->capacity;
    u32 newCapacity = GROW_CAPACITY(oldCapacity);
   
    chunk->code = GROW_ARRAY(&vm, u8, chunk->code, oldCapacity, newCapacity);  // 传入 &vm
    chunk->capacity = newCapacity;
}

// === Line Info (RLE Implementation) ===
// 3. 重构：实现 flushLineBuffer 替代 addLine
// 这个函数只在行号改变时被调用 (Cold Path)，因此不需要内联
void flushLineBuffer(Chunk* chunk, int newLine) {
    // 如果缓冲区里有数据，先将其写入 lineInfo 数组
    if (chunk->bufferedCount > 0) {
        LineInfo* info = &chunk->lineInfo;
        if (info->capacity < info->count + 1) {
            u32 oldCapacity = info->capacity;
            u32 newCapacity = GROW_CAPACITY(oldCapacity);
            info->lines = GROW_ARRAY(&vm, LineStart, info->lines, oldCapacity, newCapacity);  // 传入 &vm
            info->capacity = newCapacity;
        }
        LineStart* entry = &info->lines[info->count++];
        entry->line = chunk->bufferedLine;
        entry->count = chunk->bufferedCount;
    }
    // 重置缓冲区为新行
    chunk->bufferedLine = newLine;
    chunk->bufferedCount = 1;
}

int addConstant(Chunk* chunk, Value value) {
    // GC Hook: 确保 value 在此过程中被标记为可达
    push(&vm, value); // 传入 &vm
    writeValueArray(&chunk->constants, value);
    pop(&vm); // 传入 &vm
    // 返回常量池索引
    return chunk->constants.count - 1;
}

// === Debugging Tools ===
static int getLine(Chunk* chunk, int instructionOffset) {
    int start = 0;
   
    // 1. 遍历已归档的 RLE 数据
    for (u32 i = 0; i < chunk->lineInfo.count; i++) {
        LineStart* line = &chunk->lineInfo.lines[i];
        start += line->count;
        if (start > instructionOffset) {
            return line->line;
        }
    }
   
    // 2. 修正：如果 offset 在缓冲区范围内
    // 因为 debug 打印时可能 chunk 还没写完，部分数据还在 buffer 里
    if (chunk->bufferedCount > 0) {
        start += chunk->bufferedCount;
        if (start > instructionOffset) {
            return chunk->bufferedLine;
        }
    }
    return -1;
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    // 边界检查：防止读取越界
    if (offset + 1 >= (int)chunk->count) {  // 铸型 u32 到 int 避免警告
        printf("%-16s <EOF>\n", name);
        return offset + 1;
    }
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    if (offset + 1 >= (int)chunk->count) return offset + 1;
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
   
    // 增加边界检查
    if (constant < chunk->constants.count) {
        printValue(chunk->constants.values[constant]);
    } else {
        printf("<invalid>"); // 替换以避免 trigraphs
    }
   
    printf("'\n");
    return offset + 2;
}

// 增加 Long Constant 的反汇编支持
static int constantLongInstruction(const char* name, Chunk* chunk, int offset) {
    if (offset + 3 >= (int)chunk->count) return offset + 1;
    // 读取 24-bit 或 16-bit 索引 (取决于你的 OP_CONSTANT_LONG 实现)
    // 这里假设是 3 字节指令：OP | byte1 | byte2 (Little Endian u16)
    uint32_t constant = chunk->code[offset + 1] |
                        (chunk->code[offset + 2] << 8);
   
    printf("%-16s %4d '", name, constant);
    if (constant < chunk->constants.count) {
        printValue(chunk->constants.values[constant]);
    } else {
        printf("<invalid>");
    }
    printf("'\n");
    return offset + 3;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
   
    int line = getLine(chunk, offset);
    if (offset > 0 && line == getLine(chunk, offset - 1)) {
        printf(" | ");
    } else {
        printf("%4d ", line);
    }
    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_RETURN: return simpleInstruction("OP_RETURN", offset);
        case OP_CONSTANT: return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG: return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_NEGATE: return simpleInstruction("OP_NEGATE", offset);
        case OP_ADD: return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT: return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY: return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE: return simpleInstruction("OP_DIVIDE", offset);
        case OP_LESS: return simpleInstruction("OP_LESS", offset);
        case OP_POP: return simpleInstruction("OP_POP", offset);
        case OP_PRINT: return simpleInstruction("OP_PRINT", offset);
       
        case OP_DEFINE_GLOBAL: return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL: return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL: return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_CALL: return byteInstruction("OP_CALL", chunk, offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    // 确保把缓冲区里剩下的行号也打印出来（不需要真正 flush，只是 getLine 需要意识到它）
    // 当前的 getLine 实现已经处理了 buffer
   
    for (u32 offset = 0; offset < chunk->count;) {  // offset 改为 u32 避免警告
        offset = (u32)disassembleInstruction(chunk, (int)offset);  // 铸型回 int 如果需要
    }
}
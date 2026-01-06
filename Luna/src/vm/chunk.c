// src/vm/chunk.c

#include <stdio.h>
#include "memory.h"
#include "vm.h"
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    initValueArray(&chunk->constants);
  
    // RLE Line Info Initialization
    chunk->lineInfo.count = 0;
    chunk->lineInfo.capacity = 0;
    chunk->lineInfo.lines = NULL;
    chunk->bufferedLine = -1;
    chunk->bufferedCount = 0;
}
// [修改] 增加 VM* vm 参数
void freeChunk(VM* vm, Chunk* chunk) {
    FREE_ARRAY(vm, u8, chunk->code, chunk->capacity);
    FREE_ARRAY(vm, LineStart, chunk->lineInfo.lines, chunk->lineInfo.capacity);
  
    // [修改] 传递 vm 给 freeValueArray (假设 value.c 也会同步修改)
    freeValueArray(vm, &chunk->constants);
    initChunk(chunk);
}
// === Cold Path: Code Expansion ===
// [修改] 增加 VM* vm 参数
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void growChunkCode(VM* vm, Chunk* chunk) {
    u32 oldCapacity = chunk->capacity;
    u32 newCapacity = GROW_CAPACITY(oldCapacity);
  
    // [修改] 使用传入的 vm
    chunk->code = GROW_ARRAY(vm, u8, chunk->code, oldCapacity, newCapacity);
    chunk->capacity = newCapacity;
}
// === Line Info (RLE Implementation) ===
// [修改] 增加 VM* vm 参数，因为需要 GROW_ARRAY
void flushLineBuffer(VM* vm, Chunk* chunk, i32 newLine) {
    if (chunk->bufferedCount > 0) {
        LineInfo* info = &chunk->lineInfo;
        if (info->capacity < info->count + 1) {
            u32 oldCapacity = info->capacity;
            u32 newCapacity = GROW_CAPACITY(oldCapacity);
            // [修改] 使用传入的 vm
            info->lines = GROW_ARRAY(vm, LineStart, info->lines, oldCapacity, newCapacity);
            info->capacity = newCapacity;
        }
        LineStart* entry = &info->lines[info->count++];
        entry->line = chunk->bufferedLine;
        entry->count = chunk->bufferedCount;
    }
    chunk->bufferedLine = newLine;
    chunk->bufferedCount = 1;
}
// [修改] 增加 VM* vm 参数
i32 addConstant(VM* vm, Chunk* chunk, Value value) {
    // GC Hook: 确保 value 在此过程中被标记为可达
    // [修改] 使用传入的 vm 进行栈操作
    push(vm, value);
   
    // [修改] 传递 vm 给 writeValueArray (假设 value.h/c 会同步修改以支持扩容时的 vm 传递)
    writeValueArray(vm, &chunk->constants, value);
   
    pop(vm);
    return (i32)chunk->constants.count - 1;
}

// --- Debugging ---
static i32 getLine(Chunk* chunk, i32 instructionOffset) {
    i32 start = 0;
    for (u32 i = 0; i < chunk->lineInfo.count; i++) {
        LineStart* line = &chunk->lineInfo.lines[i];
        start += (i32)line->count;
        if (start > instructionOffset) {
            return line->line;
        }
    }
    if (chunk->bufferedCount > 0) {
        start += (i32)chunk->bufferedCount;
        if (start > instructionOffset) {
            return chunk->bufferedLine;
        }
    }
    return -1;
}
static i32 simpleInstruction(const char* name, i32 offset) {
    printf("%s\n", name);
    return offset + 1;
}
static i32 byteInstruction(const char* name, Chunk* chunk, i32 offset) {
    if (offset + 1 >= (i32)chunk->count) {
        printf("%-16s <EOF>\n", name);
        return offset + 1;
    }
    u8 slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}
static i32 constantInstruction(const char* name, Chunk* chunk, i32 offset) {
    if (offset + 1 >= (i32)chunk->count) return offset + 1;
    u8 constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
  
    if (constant < chunk->constants.count) {
        printValue(chunk->constants.values[constant]);
    } else {
        printf("<invalid>");
    }
  
    printf("'\n");
    return offset + 2;
}
static i32 constantLongInstruction(const char* name, Chunk* chunk, i32 offset) {
    if (offset + 3 >= (i32)chunk->count) return offset + 1;
    u32 constant = chunk->code[offset + 1] |
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

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

i32 disassembleInstruction(Chunk* chunk, i32 offset) {
    printf("%04d ", offset);
  
    i32 line = getLine(chunk, offset);
    if (offset > 0 && line == getLine(chunk, offset - 1)) {
        printf(" | ");
    } else {
        printf("%4d ", line);
    }
    u8 instruction = chunk->code[offset];
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
        case OP_LESS_EQUAL: return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_GREATER: return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL: return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_EQUAL: return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL: return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_NOT: return simpleInstruction("OP_NOT", offset);
        case OP_POP: return simpleInstruction("OP_POP", offset);
        case OP_PRINT: return simpleInstruction("OP_PRINT", offset);
        case OP_JUMP_IF_FALSE: return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP: return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_LOOP: return jumpInstruction("OP_LOOP", -1, chunk, offset);
      
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
    for (u32 offset = 0; offset < chunk->count;) {
        offset = (u32)disassembleInstruction(chunk, (i32)offset);
    }
}
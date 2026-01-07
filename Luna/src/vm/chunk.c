// src/vm/chunk.c

#include "memory.h"
#include "vm.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    initValueArray(&chunk->constants);

    chunk->lineInfo.count = 0;
    chunk->lineInfo.capacity = 0;
    chunk->lineInfo.lines = NULL;

    chunk->bufferedLine = -1;
    chunk->bufferedCount = 0;
}

void freeChunk(VM* vm, Chunk* chunk) {
    FREE_ARRAY(vm, u8, chunk->code, chunk->capacity);
    FREE_ARRAY(vm, LineStart, chunk->lineInfo.lines, chunk->lineInfo.capacity);
    freeValueArray(vm, &chunk->constants);
    initChunk(chunk);
}

// Cold Path: Code Expansion
void growChunkCode(VM* vm, Chunk* chunk) {
    u32 oldCapacity = chunk->capacity;
    u32 newCapacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(vm, u8, chunk->code, oldCapacity, newCapacity);
    chunk->capacity = newCapacity;
}

void flushLineBuffer(VM* vm, Chunk* chunk, i32 newLine) {
    if (chunk->bufferedCount > 0) {
        LineInfo* info = &chunk->lineInfo;
        if (info->capacity < info->count + 1) {
            u32 oldCapacity = info->capacity;
            u32 newCapacity = GROW_CAPACITY(oldCapacity);
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

i32 addConstant(VM* vm, Chunk* chunk, Value value) {
    push(vm, value);
    writeValueArray(vm, &chunk->constants, value);
    pop(vm);
    return (i32)chunk->constants.count - 1;
}

// --- Debugging ---
i32 getLine(Chunk* chunk, i32 instructionOffset) {
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
    u8 slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static i32 constantInstruction(const char* name, Chunk* chunk, i32 offset) {
    u8 constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}

static i32 constantLongInstruction(const char* name, Chunk* chunk, i32 offset) {
    u32 constant = chunk->code[offset + 1] | (chunk->code[offset + 2] << 8);
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

static i32 jumpInstruction(const char* name, i32 sign, Chunk* chunk, i32 offset) {
    u16 jump = (u16)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

static i32 invokeInstruction(const char* name, Chunk* chunk, i32 offset) {
    u8 constant = chunk->code[offset + 1];
    u8 argCount = chunk->code[offset + 2];
    printf("%-16s (%d args) %4d '", name, argCount, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
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
        case OP_CONSTANT:       return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG:  return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_NIL:            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:           return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:          return simpleInstruction("OP_FALSE", offset);
        case OP_POP:            return simpleInstruction("OP_POP", offset);
        case OP_GET_LOCAL:      return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:      return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:     return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:  return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:     return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE:    return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:    return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_EQUAL:          return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:      return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_GREATER:        return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:  return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS:           return simpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:     return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_ADD:            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:       return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:       return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:         return simpleInstruction("OP_DIVIDE", offset);
        case OP_NOT:            return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:         return simpleInstruction("OP_NEGATE", offset);
        case OP_PRINT:          return simpleInstruction("OP_PRINT", offset);
        case OP_JUMP:           return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:  return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:           return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_CHECK_DEFAULT: {
            u8 slot = chunk->code[offset + 1];
            u16 jump = (u16)(chunk->code[offset + 2] << 8) | chunk->code[offset + 3];
            printf("%-16s %4d -> %d\n", "OP_CHECK_DEFAULT", slot, offset + 4 + jump);
            return offset + 4;
        }
        case OP_CALL_KW: {
            u8 argCount = chunk->code[offset + 1];
            u8 kwCount = chunk->code[offset + 2];
            printf("%-16s %d args, %d kws\n", "OP_CALL_KW", argCount, kwCount);
            return offset + 3;
        }
        case OP_CALL:           return byteInstruction("OP_CALL", chunk, offset);
        case OP_BUILD_LIST:     return byteInstruction("OP_BUILD_LIST", chunk, offset);
        case OP_BUILD_DICT:     return byteInstruction("OP_BUILD_DICT", chunk, offset);
        case OP_INVOKE:         return invokeInstruction("OP_INVOKE", chunk, offset);
        case OP_SUPER_INVOKE:   return invokeInstruction("OP_SUPER_INVOKE", chunk, offset);
        case OP_CLOSURE: {
            offset++;
            u8 constant = chunk->code[offset++];
            printf("%-16s %4d ", "OP_CLOSURE", constant);
            printValue(chunk->constants.values[constant]);
            printf("\n");
            
            ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
            for (i32 j = 0; j < function->upvalueCount; j++) {
                i32 isLocal = chunk->code[offset++];
                i32 index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n",
                       offset - 2, isLocal ? "local" : "upvalue", index);
            }
            return offset;
        }
        case OP_CLOSE_UPVALUE:  return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_RETURN:         return simpleInstruction("OP_RETURN", offset);
        case OP_CLASS:          return constantInstruction("OP_CLASS", chunk, offset);
        case OP_INHERIT:        return simpleInstruction("OP_INHERIT", offset);
        case OP_METHOD:         return constantInstruction("OP_METHOD", chunk, offset);
        case OP_GET_PROPERTY:   return constantInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:   return constantInstruction("OP_SET_PROPERTY", chunk, offset);
        case OP_GET_SUPER:      return constantInstruction("OP_GET_SUPER", chunk, offset);
            
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
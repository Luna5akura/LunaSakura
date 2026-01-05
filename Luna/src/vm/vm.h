// src/vm/vm.h

#pragma once

#include "table.h"
#include "chunk.h"
#include "object.h"
#include "engine/timeline.h"  // Added for Timeline* definition

// --- Configuration ---
#define STACK_MAX 1024
#define FRAMES_MAX 64

// --- Call Frame ---
typedef struct {
    ObjClip* clip;
    u8* ip;
    Value* slots;
} CallFrame;

// --- Result Code ---
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// --- VM Structure ---
struct VM {
    // --- Hot Path Data ---
    Value* stackTop;
  
    int frameCount;
    CallFrame frames[FRAMES_MAX];
    Chunk* chunk;
    u8* ip;
    // --- Global State ---
    Table globals;
    Table strings;
    // --- Garbage Collection ---
    Obj* objects;
  
    int grayCount;
    int grayCapacity;
    Obj** grayStack;
    size_t bytesAllocated;
    size_t nextGC;
    Timeline* active_timeline;  // Added: Instance-specific active timeline
    // --- Storage ---
    Value stack[STACK_MAX];
};

// --- API (Context-Aware) ---
void initVM(VM* vm);
void freeVM(VM* vm);
void defineNative(VM* vm, const char* name, NativeFn function);
InterpretResult interpret(VM* vm, Chunk* chunk);

// --- Stack Operations ---
static INLINE void resetStack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
}
static INLINE void push(VM* vm, Value value) {
#ifdef DEBUG_TRACE_EXECUTION
    if (vm->stackTop >= vm->stack + STACK_MAX) {
        // Handle overflow
        return;
    }
#endif
    *vm->stackTop = value;
    vm->stackTop++;
}
static INLINE Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}
static INLINE Value peek(VM* vm, int distance) {
    return vm->stackTop[-1 - distance];
}
void runtimeError(VM* vm, const char* format, ...);
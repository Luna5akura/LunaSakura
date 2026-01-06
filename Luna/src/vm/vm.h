// src/vm/vm.h

#pragma once
#include "table.h"
#include "chunk.h"
#include "object.h"
#include "engine/timeline.h" // Added for Timeline* definition
// --- Configuration ---
#define STACK_MAX 1024
#define FRAMES_MAX 64
// --- Call Frame ---

void runtimeError(VM* vm, const char* format, ...);

typedef struct {
    ObjFunction* function; // 当前执行的函数
    u8* ip;                // 指令指针
    Value* slots;          // 局部变量在 VM 栈上的起始位置
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
 
    i32 frameCount;
    CallFrame frames[FRAMES_MAX];
    // Chunk* chunk;
    // u8* ip;
    // --- Global State ---
    Table globals;
    Table strings;
    // --- Garbage Collection ---
    Obj* objects;
 
    i32 grayCount;
    i32 grayCapacity;
    Obj** grayStack;
    size_t bytesAllocated;
    size_t nextGC;
    Timeline* active_timeline; // Added: Instance-specific active timeline
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
    if (vm->stackTop >= vm->stack + STACK_MAX) {
        runtimeError(vm, "Stack overflow.");
        // exit(1);  // 或其他处理
    }
    *vm->stackTop = value;
    vm->stackTop++;
}
static INLINE Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}
static INLINE Value peek(VM* vm, i32 distance) {
    return vm->stackTop[-1 - distance];
}
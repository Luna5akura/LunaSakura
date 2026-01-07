// src/vm/vm.h

#pragma once
#include "object.h"
#include "engine/timeline.h"
// --- Configuration ---
#define STACK_MAX 1024
#define FRAMES_MAX 64
// --- Call Frame ---
typedef struct {
    ObjClosure* closure; // [修改] 现在指向闭包
    u8* ip;
    Value* slots;
} CallFrame;
// --- Result Code ---
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;
void runtimeError(VM* vm, const char* format, ...);
// --- VM Structure ---
struct VM {
    // --- Hot Path Data ---
    Value* stackTop;
    i32 frameCount;
    CallFrame frames[FRAMES_MAX];
    // --- Global State ---
    Table globals;
    Table strings;
    ObjString* initString;
    ObjUpvalue* openUpvalues; // [新增] 打开的上值链表
    // --- Garbage Collection ---
    Obj* objects;
    i32 grayCount;
    i32 grayCapacity;
    Obj** grayStack;
    size_t bytesAllocated;
    size_t nextGC;
    Timeline* active_timeline;
    // --- Storage ---
    Value stack[STACK_MAX];
};
// --- API ---
void initVM(VM* vm);
void freeVM(VM* vm);
void defineNative(VM* vm, const char* name, NativeFn function);
InterpretResult interpret(VM* vm, Chunk* chunk);
// --- Stack Operations ---
static inline void resetStack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL; // [新增]
}
static inline void push(VM* vm, Value value) {
    if (vm->stackTop >= vm->stack + STACK_MAX) {
        runtimeError(vm, "Stack overflow.");
        // exit(1);
    }
    *vm->stackTop = value;
    vm->stackTop++;
}
static inline Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}
static inline Value peek(VM* vm, i32 distance) {
    return vm->stackTop[-1 - distance];
}
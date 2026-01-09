// src/core/vm/vm.h

#pragma once
#include "error.h"
#include "core/object.h"
#include "engine/timeline.h"
// --- Configuration ---
#define STACK_MAX 2048
#define FRAMES_MAX 64
// --- Call Frame ---
typedef struct {
    ObjClosure* closure;
    u8* ip;
    Value* slots; // 指向 VM 栈中该帧的起始位置
} CallFrame;
// --- Result Code ---
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;
// --- Exception Handler ---
typedef struct sHandler {
    int frameIndex;
    u8* handlerIp;
    Value* tryStackTop;
} Handler;
// --- VM Structure ---
struct VM {
    // --- Hot Path Data (放在结构体开头以优化缓存局部性) ---
    Value* stackTop;
    CallFrame* frames;
    i32 frameCount;
 
    // --- Global State ---
    Table globals;
    Table strings;
    ObjString* initString;
    ObjUpvalue* openUpvalues;
 
    // --- Garbage Collection ---
    size_t bytesAllocated;
    size_t nextGC;
    Obj* objects;
    i32 grayCount;
    i32 grayCapacity;
    Obj** grayStack;
 
    // --- Engine State ---
    Project* active_project;
 
    // --- Exception Handling ---
    Handler handlers[FRAMES_MAX];
    int handlerCount;
 
    // --- Storage ---
    Value stack[STACK_MAX];
    CallFrame framesStorage[FRAMES_MAX];
};
// --- API ---
void initVM(VM* vm);
void freeVM(VM* vm);
// 核心解释函数
InterpretResult interpret(VM* vm, Chunk* chunk);
// 定义原生函数
void defineNative(VM* vm, const char* name, NativeFn function);
// 共享的 VM 操作
void closeUpvalues(VM* vm, Value* last);
ObjUpvalue* captureUpvalue(VM* vm, Value* local); // 暴露给 vm_handler.h 使用
// --- Stack Operations (Inline) ---
static INLINE void resetStack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->frames = vm->framesStorage;
}
static INLINE bool push(VM* vm, Value value) {
    if (UNLIKELY(vm->stackTop >= vm->stack + STACK_MAX)) {
        runtimeError(vm, "Stack overflow.");
        return false; // Indicate failure to propagate error
    }
    *vm->stackTop++ = value;
    return true;
}
static INLINE Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}
static INLINE Value peek(VM* vm, i32 distance) {
    return vm->stackTop[-1 - distance];
}

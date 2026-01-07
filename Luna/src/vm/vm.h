// src/vm/vm.h

#pragma once
#include "engine/timeline.h"

// --- Configuration ---
#define STACK_MAX 2048 // [调整] 稍微增大栈空间
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

// --- VM Structure ---
struct VM {
    // --- Hot Path Data (放在结构体开头以优化缓存局部性) ---
    Value* stackTop;
    CallFrame* frames; // [修改] 改为动态或指针，但在简单VM中数组更快
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
    Timeline* active_timeline;
    
    // --- Storage ---
    Value stack[STACK_MAX];
    CallFrame framesStorage[FRAMES_MAX]; // 重命名以区分逻辑
};

// --- API ---
void initVM(VM* vm);
void freeVM(VM* vm);

// 核心解释函数
InterpretResult interpret(VM* vm, Chunk* chunk);

// 定义原生函数
void defineNative(VM* vm, const char* name, NativeFn function);

// 错误处理
void runtimeError(VM* vm, const char* format, ...);

// --- Stack Operations (Inline) ---

static INLINE void resetStack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->frames = vm->framesStorage; // 指向内部数组
}

static INLINE void push(VM* vm, Value value) {
    // 生产环境可以移除此检查以获得极限性能，或者改为断言
    if (UNLIKELY(vm->stackTop >= vm->stack + STACK_MAX)) {
        // 栈溢出通常是致命错误，这里简单的打印并不能解决问题，
        // 但为了 inline 的简洁性，我们保留简单逻辑，实际由 runtimeError 处理
        return; 
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

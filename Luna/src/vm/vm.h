// src/vm/vm.h

#pragma once // 1. 编译优化
#include "common.h"
#include "value.h"
#include "table.h"
#include "chunk.h"
#include "object.h"

// --- Configuration ---
// 最大栈深度（Value Stack）
#define STACK_MAX 1024
// 最大调用深度（Call Frame Stack）
#define FRAMES_MAX 64

// --- Call Frame ---
// 2. 新增：函数调用栈帧
// 保存当前正在执行的函数上下文，以便 OP_RETURN 时恢复
typedef struct {
    ObjClip* clip; // 当前执行的函数/片段对象 (closure)
    u8* ip; // 指向该函数 Chunk 中的当前指令地址
    Value* slots; // 指向 ValueStack 中该函数局部变量的起始位置
} CallFrame;

// --- VM Structure ---
typedef struct {
    // --- Hot Path Data (Cache Line 1) ---
    // 将栈顶指针放在最前，访问最频繁
    Value* stackTop;
   
    // 当前帧的缓存（优化：避免频繁访问 frames 数组）
    int frameCount; // 修改为整数索引
    CallFrame frames[FRAMES_MAX];
    Chunk* chunk; // 当前 chunk
    u8* ip; // 当前 ip
    // --- Global State ---
    Table globals;
    Table strings; // 字符串驻留池 (String Interning)
    // --- Garbage Collection ---
    Obj* objects; // 所有分配对象的链表头
   
    // 3. 新增：GC 灰色栈 (Gray Stack for Mark-Sweep)
    // 用于在 GC 标记阶段暂存待处理对象
    int grayCount;
    int grayCapacity;
    Obj** grayStack;

    // 新增：GC 统计字段（修复缺失成员）
    size_t bytesAllocated;  // 当前分配字节数
    size_t nextGC;  // 下次 GC 阈值（例如初始为 1024 * 1024）

    // --- Storage ---
    // 放在最后，避免影响头部字段的缓存偏移量
    Value stack[STACK_MAX];
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// --- API (Context-Aware) ---
// 1. 优化：移除全局 'vm'，所有函数接收指针
void initVM(VM* vm);
void freeVM(VM* vm);
void defineNative(VM* vm, const char* name, NativeFn function);
// 修改为接收 Chunk 以匹配用户代码
InterpretResult interpret(VM* vm, Chunk* chunk);

// --- Stack Operations (Inlined & Safe) ---
// 4. 优化：增加 resetStack
static INLINE void resetStack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
}
static INLINE void push(VM* vm, Value value) {
    // Debug 模式下的边界检查
#ifdef DEBUG_TRACE_EXECUTION
    if (vm->stackTop >= vm->stack + STACK_MAX) {
        // Handle overflow (abort or print error)
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
    // distance 0 is the top element (stackTop - 1)
    return vm->stackTop[-1 - distance];
}
// 5. 运行时错误报告辅助函数
// 使用可变参数处理格式化字符串
void runtimeError(VM* vm, const char* format, ...);
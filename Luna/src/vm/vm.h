//src/vm/vm.h

#ifndef LUNA_VM_H
#define LUNA_VM_H

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

// 栈的最大深度（暂时定为 256）
#define STACK_MAX 256

typedef struct {
    Chunk* chunk;      // 当前正在执行的代码块
    uint8_t* ip;       // 指令指针 (Instruction Pointer)，指向下一条要执行的指令
    
    Value stack[STACK_MAX]; // 核心：操作数栈
    Value* stackTop;        // 栈顶指针 (指向栈中下一个空闲位置)

    Table globals; // 全局变量表
    Table strings; // 字符串池
} VM;

// 解释器结果枚举
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm; 

void initVM();
void freeVM();


void defineNative(const char* name, NativeFn function);


// 核心入口：执行一个 Chunk
InterpretResult interpret(Chunk* chunk);

// 栈操作函数（给 VM 内部使用）
void push(Value value);
Value pop();

#endif
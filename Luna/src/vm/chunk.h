// src/vm/chunk.h

#ifndef LUNA_CHUNK_H
#define LUNA_CHUNK_H

#include "common.h"
#include "value.h"

// 1. 指令集 (OpCode)
typedef enum {
    OP_CONSTANT, // 加载常量
    OP_NEGATE,   // <--- 新增：取负号 (比如 -10)

    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,

    OP_POP,           // 弹出栈顶（用于语句结束）
    OP_DEFINE_GLOBAL, // 定义全局变量
    OP_GET_GLOBAL,    // 获取全局变量
    OP_SET_GLOBAL,    // 设置全局变量

    OP_PRINT, // 新增
    OP_CALL, // <--- 新增：函数调用
    OP_RETURN,   // 返回/结束
} OpCode;

// 2. 字节码块 (Chunk)
typedef struct {
    int count;
    int capacity;
    uint8_t* code;      // 指令流 (一堆字节)
    ValueArray constants; // 常量池 (存数字、字符串等)
    // int* lines;      // 将来用于报错时显示行号
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
// 写入指令
void writeChunk(Chunk* chunk, uint8_t byte);
// 写入常量，返回常量在池中的索引
int addConstant(Chunk* chunk, Value value);

int disassembleInstruction(Chunk* chunk, int offset);
// 反汇编（调试用）：把二进制指令打印成人类能看的汇编代码
void disassembleChunk(Chunk* chunk, const char* name);

#endif
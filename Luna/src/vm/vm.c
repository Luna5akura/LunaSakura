// src/vm/vm.c

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "memory.h"
#include "vm.h"
// === Helper Functions ===
void runtimeError(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    resetStack(vm);
}
void initVM(VM* vm) {
    // 显式清零，避免垃圾数据
    memset(vm, 0, sizeof(VM));
 
    resetStack(vm);
    vm->objects = NULL;
    initTable(&vm->globals);
    initTable(&vm->strings);
 
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;
    vm->frameCount = 0;
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->active_timeline = NULL; // Added: Initialize active timeline
}
void freeVM(VM* vm) {
    // [修改] 传递 vm 上下文给 freeTable，因为释放内存需要 vm->bytesAllocated 统计
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->strings);
    freeObjects(vm);
 
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;
 
    if (vm->grayStack) free(vm->grayStack); // grayStack 是纯指针数组，直接 free 即可
    vm->grayStack = NULL;
    vm->grayCapacity = 0;
    vm->grayCount = 0;
    if (vm->active_timeline) { // Added: Clean up active timeline
        timeline_free(vm, vm->active_timeline);
        vm->active_timeline = NULL;
    }
}
void defineNative(VM* vm, const char* name, NativeFn function) {
    // [修改] 传递 vm 给 copyString
    ObjString* string = copyString(vm, name, (i32)strlen(name));
    if (string == NULL) {
        runtimeError(vm, "Failed to allocate string for native function '%s'.", name);
        return;
    }
    push(vm, OBJ_VAL(string));
 
    // [修改] 传递 vm 给 newNative
    ObjNative* native = newNative(vm, function);
    if (native == NULL) {
        runtimeError(vm, "Failed to allocate native function object for '%s'.", name);
        pop(vm);
        return;
    }
    push(vm, OBJ_VAL(native));
    // [修改] 传递 vm 给 tableSet (用于潜在的扩容内存分配)
    tableSet(vm, &vm->globals, string, vm->stack[1]);
    pop(vm);
    pop(vm);
}
static bool callValue(VM* vm, Value callee, i32 argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                // [修改] 将 vm 传递给 Native 函数
                // 对应 bind_video.c 中的函数签名修改
                Value result = native(vm, argCount, vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }
            // TODO: Handle other callable types (e.g., Lox Functions, Classes)
            default:
                break;
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}
// === The Interpreter Core ===
static InterpretResult run(VM* vm) {
    register u8* ip = vm->ip;
    register Value* stackTop = vm->stackTop;
#ifdef COMPUTED_GOTO
    static void* dispatchTable[] = {
        &&TARGET_OP_CONSTANT,
        &&TARGET_OP_CONSTANT_LONG,
        &&TARGET_OP_NEGATE,
        &&TARGET_OP_ADD,
        &&TARGET_OP_SUBTRACT,
        &&TARGET_OP_MULTIPLY,
        &&TARGET_OP_DIVIDE,
        &&TARGET_OP_LESS,
        &&TARGET_OP_LESS_EQUAL,
        &&TARGET_OP_GREATER,
        &&TARGET_OP_GREATER_EQUAL,
        &&TARGET_OP_EQUAL,
        &&TARGET_OP_NOT_EQUAL,
        &&TARGET_OP_NOT,
        &&TARGET_OP_POP,
        &&TARGET_OP_JUMP_IF_FALSE, // [新增]
        &&TARGET_OP_JUMP, // [新增]
        &&TARGET_OP_DEFINE_GLOBAL,
        &&TARGET_OP_GET_GLOBAL,
        &&TARGET_OP_SET_GLOBAL,
        &&TARGET_OP_PRINT,
        &&TARGET_OP_CALL,
        &&TARGET_OP_RETURN,
    };
    #define DISPATCH() goto *dispatchTable[*ip++]
    #define CASE(name) TARGET_##name:
#else
    #define DISPATCH() break
    #define CASE(name) case name:
#endif
    #define READ_BYTE() (*ip++)
    #define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define BINARY_OP(valueType, op) \
        do { \
            if (UNLIKELY(!IS_NUMBER(stackTop[-1]) || !IS_NUMBER(stackTop[-2]))) { \
                vm->ip = ip; vm->stackTop = stackTop; \
                runtimeError(vm, "Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(stackTop[-1]); \
            double a = AS_NUMBER(stackTop[-2]); \
            stackTop--; \
            stackTop[-1] = valueType(a op b); \
        } while (false)
    #define BINARY_BOOL_OP(op) \
        do { \
            if (UNLIKELY(!IS_NUMBER(stackTop[-1]) || !IS_NUMBER(stackTop[-2]))) { \
                vm->ip = ip; vm->stackTop = stackTop; \
                runtimeError(vm, "Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(stackTop[-1]); \
            double a = AS_NUMBER(stackTop[-2]); \
            stackTop--; \
            stackTop[-1] = BOOL_VAL(a op b); \
        } while (false)
    #define READ_SHORT() \
        (ip += 2, (u16)((ip[-2] << 8) | ip[-1]))
#ifdef COMPUTED_GOTO
    DISPATCH();
#else
    for (;;) {
        u8 instruction = READ_BYTE();
        switch (instruction) {
#endif
    CASE(OP_CONSTANT) {
        Value constant = READ_CONSTANT();
        *stackTop = constant;
        stackTop++;
        DISPATCH();
    }
    CASE(OP_CONSTANT_LONG) {
        // 假设是 3 字节指令
        u32 index = READ_BYTE();
        index |= (u16)READ_BYTE() << 8;
        index |= (u32)READ_BYTE() << 16;
        DISPATCH();
    }
    CASE(OP_NEGATE) {
        if (UNLIKELY(!IS_NUMBER(stackTop[-1]))) {
            vm->ip = ip; vm->stackTop = stackTop;
            runtimeError(vm, "Operand must be a number.");
            return INTERPRET_RUNTIME_ERROR;
        }
        stackTop[-1] = NUMBER_VAL(-AS_NUMBER(stackTop[-1]));
        DISPATCH();
    }
    CASE(OP_NOT) {
        if (UNLIKELY(!IS_BOOL(stackTop[-1]))) {
            vm->ip = ip; vm->stackTop = stackTop;
            runtimeError(vm, "Operand must be a boolean.");
            return INTERPRET_RUNTIME_ERROR;
        }
        stackTop[-1] = BOOL_VAL(!AS_BOOL(stackTop[-1]));
        DISPATCH();
    }
    CASE(OP_ADD) {
        if (IS_STRING(stackTop[-1]) && IS_STRING(stackTop[-2])) {
            vm->ip = ip; vm->stackTop = stackTop;
            runtimeError(vm, "String concatenation not yet implemented.");
            return INTERPRET_RUNTIME_ERROR;
        }
        BINARY_OP(NUMBER_VAL, +);
        DISPATCH();
    }
    CASE(OP_SUBTRACT) { BINARY_OP(NUMBER_VAL, -); DISPATCH(); }
    CASE(OP_MULTIPLY) { BINARY_OP(NUMBER_VAL, *); DISPATCH(); }
    CASE(OP_DIVIDE) { BINARY_OP(NUMBER_VAL, /); DISPATCH(); }
    CASE(OP_LESS) { BINARY_BOOL_OP(<); DISPATCH(); }
    CASE(OP_LESS_EQUAL) { BINARY_BOOL_OP(<=); DISPATCH(); }
    CASE(OP_GREATER) { BINARY_BOOL_OP(>); DISPATCH(); }
    CASE(OP_GREATER_EQUAL) { BINARY_BOOL_OP(>=); DISPATCH(); }
    CASE(OP_EQUAL) { BINARY_BOOL_OP(==); DISPATCH(); }
    CASE(OP_NOT_EQUAL) { BINARY_BOOL_OP(!=); DISPATCH(); }
    CASE(OP_POP) {
        stackTop--;
        DISPATCH();
    }
    // [新增] 跳转指令
    CASE(OP_JUMP_IF_FALSE) {
        u16 offset = READ_SHORT();
        if (!AS_BOOL(peek(vm, 0))) ip += offset;
        DISPATCH();
    }
    CASE(OP_JUMP) {
        u16 offset = READ_SHORT();
        ip += offset;
        DISPATCH();
    }
    CASE(OP_DEFINE_GLOBAL) {
        ObjString* name = READ_STRING();
        tableSet(vm, &vm->globals, name, stackTop[-1]);
        stackTop--;
        DISPATCH();
    }
    CASE(OP_GET_GLOBAL) {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm->globals, name, &value)) {
            vm->ip = ip; vm->stackTop = stackTop;
            runtimeError(vm, "Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        *stackTop = value;
        stackTop++;
        DISPATCH();
    }
    CASE(OP_SET_GLOBAL) {
        ObjString* name = READ_STRING();
        if (tableSet(vm, &vm->globals, name, stackTop[-1])) {
            tableDelete(&vm->globals, name);
            vm->ip = ip; vm->stackTop = stackTop;
            runtimeError(vm, "Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    CASE(OP_PRINT) {
        stackTop--;
        printValue(*stackTop);
        printf("\n");
        DISPATCH();
    }
    CASE(OP_CALL) {
        i32 argCount = READ_BYTE();
    
        // 同步状态到 VM 结构体
        vm->ip = ip;
        vm->stackTop = stackTop;
    
        Value callee = stackTop[-1 - argCount];
        // callValue 内部会处理 vm 传递
        if (!callValue(vm, callee, argCount)) {
            return INTERPRET_RUNTIME_ERROR;
        }
    
        // 恢复寄存器缓存
        stackTop = vm->stackTop;
        DISPATCH();
    }
    CASE(OP_RETURN) {
        vm->ip = ip;
        vm->stackTop = stackTop;
        return INTERPRET_OK;
    }
#ifndef COMPUTED_GOTO
        }
    }
#endif
    return INTERPRET_RUNTIME_ERROR;
    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef BINARY_OP
    #undef BINARY_BOOL_OP
    #undef READ_SHORT
    #undef DISPATCH
    #undef CASE
}
InterpretResult interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = vm->chunk->code;
    return run(vm);
}
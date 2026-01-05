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
    vm->active_timeline = NULL;  // Added: Initialize active timeline
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
    if (vm->active_timeline) {  // Added: Clean up active timeline
        timeline_free(vm, vm->active_timeline);
        vm->active_timeline = NULL;
    }
}

void defineNative(VM* vm, const char* name, NativeFn function) {
    // [修改] 传递 vm 给 copyString
    ObjString* string = copyString(vm, name, (int)strlen(name));
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

static bool callValue(VM* vm, Value callee, int argCount) {
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
        &&TARGET_OP_CONSTANT_LONG, // 补充缺失的跳转表项
        &&TARGET_OP_NEGATE,
        &&TARGET_OP_ADD,
        &&TARGET_OP_SUBTRACT,
        &&TARGET_OP_MULTIPLY,
        &&TARGET_OP_DIVIDE,
        &&TARGET_OP_LESS,
        &&TARGET_OP_POP,
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
   
    // 补充：为了完整性，这里加上 OP_CONSTANT_LONG 的处理逻辑（虽然原代码未实现细节）
    CASE(OP_CONSTANT_LONG) {
        // 假设是 3 字节指令
        uint32_t index = READ_BYTE();
        index |= (uint16_t)READ_BYTE() << 8;
        index |= (uint32_t)READ_BYTE() << 16; // 这里假设是 24-bit，具体看 compiler 实现，此处仅做占位
        // 实际上原 Chunk 实现并未完全支持 Long，暂且跳过或作为 TODO
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
  
    CASE(OP_ADD) {
        if (IS_STRING(stackTop[-1]) && IS_STRING(stackTop[-2])) {
            // TODO: implement concatenate(vm, ...)
            // 必须传入 vm 用于分配新字符串
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
  
    CASE(OP_LESS) {
        if (UNLIKELY(!IS_NUMBER(stackTop[-1]) || !IS_NUMBER(stackTop[-2]))) {
             vm->ip = ip; vm->stackTop = stackTop;
             runtimeError(vm, "Operands must be numbers.");
             return INTERPRET_RUNTIME_ERROR;
        }
        double b = AS_NUMBER(stackTop[-1]);
        double a = AS_NUMBER(stackTop[-2]);
        stackTop--;
        stackTop[-1] = BOOL_VAL(a < b);
        DISPATCH();
    }
    CASE(OP_POP) {
        stackTop--;
        DISPATCH();
    }
    CASE(OP_DEFINE_GLOBAL) {
        ObjString* name = READ_STRING();
        // [修改] 传递 vm 给 tableSet，因为可能触发扩容分配
        tableSet(vm, &vm->globals, name, stackTop[-1]);
        stackTop--;
        DISPATCH();
    }
    CASE(OP_GET_GLOBAL) {
        ObjString* name = READ_STRING();
        Value value;
        // tableGet 是只读的，通常不需要 vm，但如果为了接口一致性未来加上也可，目前保持原样
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
        // [修改] 传递 vm 给 tableSet
        if (tableSet(vm, &vm->globals, name, stackTop[-1])) {
            // 如果 Set 返回 true，说明这是一个新键（未定义），而不是赋值
            // 撤销操作并报错
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
        int argCount = READ_BYTE();
      
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
    #undef DISPATCH
    #undef CASE
}

InterpretResult interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = vm->chunk->code;
    return run(vm);
}
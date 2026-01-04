// src/vm/vm.c

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>  // 新增：为 free 等标准库函数提供声明
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

// Global VM instance (保留以兼容，但函数使用参数)
VM vm;

// === Helper Functions ===
// resetStack 已内联在 vm.h 中，无需重新定义

void runtimeError(VM* vm, const char* format, ...) {  // 移除 static 以匹配头文件声明
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
   
    // Stack trace logic would go here
    resetStack(vm);
}

void initVM(VM* vm) {
    // Explicitly zero the structure to avoid garbage values
    memset(vm, 0, sizeof(VM));
    
    resetStack(vm);
    vm->objects = NULL;
    initTable(&vm->globals);
    initTable(&vm->strings);
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;  // Initial GC threshold (adjust as needed)
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;
    vm->frameCount = 0;
    vm->chunk = NULL;
    vm->ip = NULL;
}

void freeVM(VM* vm) {
    freeTable(&vm->globals);
    freeTable(&vm->strings);
    freeObjects(vm);
    vm->bytesAllocated = 0;  // Reset allocation tracking
    vm->nextGC = 1024 * 1024;  // Reset threshold
    if (vm->grayStack) free(vm->grayStack);  // Clear gray stack
    vm->grayStack = NULL;
    vm->grayCapacity = 0;
    vm->grayCount = 0;
}

// Note: push/pop/peek are static inline in vm.h for performance.

void defineNative(VM* vm, const char* name, NativeFn function) {
    ObjString* string = copyString(name, (int)strlen(name));
    if (string == NULL) {
        runtimeError(vm, "Failed to allocate string for native function '%s'.", name);
        return;  // Or handle gracefully, e.g., skip binding
    }
    push(vm, OBJ_VAL(string));
    ObjNative* native = newNative(function);
    if (native == NULL) {
        runtimeError(vm, "Failed to allocate native function object for '%s'.", name);
        pop(vm);  // Clean up the pushed string to avoid stack imbalance
        return;
    }
    push(vm, OBJ_VAL(native));

    tableSet(&vm->globals, string, vm->stack[1]);

    pop(vm);
    pop(vm);
}

static bool callValue(VM* vm, Value callee, int argCount) {  // 添加 VM*
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }
            // TODO: Handle other callable types
            default:
                break;
        }
    }
   
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

// === The Interpreter Core ===
static InterpretResult run(VM* vm) {  // 添加 VM*
    // Optimization: Cache VM state in CPU registers.
    register u8* ip = vm->ip;
    register Value* stackTop = vm->stackTop;
#ifdef COMPUTED_GOTO
    // Label lookup table
    static void* dispatchTable[] = {
        &&TARGET_OP_CONSTANT,
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
        // Optimization: Handle String concatenation vs Arithmetic
        if (IS_STRING(stackTop[-1]) && IS_STRING(stackTop[-2])) {
            // TODO: implement concatenate() which allocates new string
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
        tableSet(&vm->globals, name, stackTop[-1]);
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
        if (tableSet(&vm->globals, name, stackTop[-1])) {
            tableDelete(&vm->globals, name); // Undo creation
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
       
        // Sync state before calling function (which might fail or GC)
        vm->ip = ip;
        vm->stackTop = stackTop;
       
        // Callee is at: stackTop - argCount - 1
        Value callee = stackTop[-1 - argCount];
        if (!callValue(vm, callee, argCount)) {
            return INTERPRET_RUNTIME_ERROR;
        }
       
        // Restore cached register from global state (it might have changed)
        stackTop = vm->stackTop;
        DISPATCH();
    }
    CASE(OP_RETURN) {
        // Exit interpreter loop
        vm->ip = ip;
        vm->stackTop = stackTop;
        return INTERPRET_OK;
    }
#ifndef COMPUTED_GOTO
        } // end switch
    } // end for
#endif
    return INTERPRET_RUNTIME_ERROR; // Should not reach here
    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef BINARY_OP
    #undef DISPATCH
    #undef CASE
}

InterpretResult interpret(VM* vm, Chunk* chunk) {  // 修改签名匹配
    vm->chunk = chunk;
    vm->ip = vm->chunk->code;
    return run(vm);  // 传入 vm
}
// src/vm/vm.c

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"
#include "object.h" 
#include "chunk.h"

void runtimeError(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (i32 i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", getLine(&function->chunk, (int)instruction));
        
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    resetStack(vm);
}

void initVM(VM* vm) {
    memset(vm, 0, sizeof(VM));
    resetStack(vm);
    vm->objects = NULL;
    initTable(&vm->globals);
    initTable(&vm->strings);
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;
}

void freeVM(VM* vm) {
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->strings);
    freeObjects(vm);
    if (vm->grayStack) free(vm->grayStack);
    if (vm->active_timeline) timeline_free(vm, vm->active_timeline);
}

void defineNative(VM* vm, const char* name, NativeFn function) {
    ObjString* nameObj = copyString(vm, name, (i32)strlen(name));
    push(vm, OBJ_VAL(nameObj));  // 保护name
    ObjNative* nativeObj = newNative(vm, function);
    push(vm, OBJ_VAL(nativeObj));  // 保护native
    tableSet(vm, &vm->globals, nameObj, OBJ_VAL(nativeObj));
    pop(vm);  // native
    pop(vm);  // name
}

static bool call(VM* vm, ObjFunction* function, i32 argCount) {
    if (argCount != function->arity) {
        runtimeError(vm, "Expected %d arguments but got %d.", function->arity, argCount);
        return false;
    }
    if (vm->frameCount == FRAMES_MAX) {
        runtimeError(vm, "Stack overflow.");
        return false;
    }
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}

static bool callValue(VM* vm, Value callee, i32 argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(vm, argCount, vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }
            case OBJ_FUNCTION:
                return call(vm, AS_FUNCTION(callee), argCount);
            default: break;
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    register u8* ip = frame->ip;

    #define READ_BYTE() (*ip++)
    #define READ_SHORT() (ip += 2, (u16)((ip[-2] << 8) | ip[-1]))
    #define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    
    #define BINARY_OP(valueType, op) \
        do { \
            if (UNLIKELY(!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1)))) { \
                frame->ip = ip; \
                runtimeError(vm, "Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop(vm)); \
            double a = AS_NUMBER(pop(vm)); \
            push(vm, valueType(a op b)); \
        } while (false)

    #define BINARY_BOOL_OP(op) \
        do { \
            if (UNLIKELY(!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1)))) { \
                frame->ip = ip; \
                runtimeError(vm, "Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop(vm)); \
            double a = AS_NUMBER(pop(vm)); \
            push(vm, BOOL_VAL(a op b)); \
        } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        // ... (Optional Trace)
#endif
        u8 instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: push(vm, READ_CONSTANT()); break;
            case OP_CONSTANT_LONG: {
                u32 index = READ_BYTE();
                index |= (u16)READ_BYTE() << 8;
                index |= (u32)READ_BYTE() << 16;
                push(vm, frame->function->chunk.constants.values[index]);
                break;
            }
            case OP_NIL: push(vm, NIL_VAL); break;
            case OP_TRUE: push(vm, TRUE_VAL); break;
            case OP_FALSE: push(vm, FALSE_VAL); break;
            
            case OP_POP: pop(vm); break;
            
            case OP_GET_LOCAL: {
                u8 slot = READ_BYTE();
                push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                u8 slot = READ_BYTE();
                frame->slots[slot] = peek(vm, 0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm->globals, name, &value)) {
                    frame->ip = ip;
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(vm, &vm->globals, name, peek(vm, 0));
                pop(vm);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(vm, &vm->globals, name, peek(vm, 0))) {
                    tableDelete(&vm->globals, name);
                    frame->ip = ip;
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(!valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:       BINARY_BOOL_OP(>); break;
            case OP_GREATER_EQUAL: BINARY_BOOL_OP(>=); break;
            case OP_LESS:          BINARY_BOOL_OP(<); break;
            case OP_LESS_EQUAL:    BINARY_BOOL_OP(<=); break;
            case OP_ADD:           BINARY_OP(NUMBER_VAL, +); break;
            case OP_SUBTRACT:      BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY:      BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:        BINARY_OP(NUMBER_VAL, /); break;
            
            case OP_NOT: push(vm, BOOL_VAL(!AS_BOOL(pop(vm)))); break;
            case OP_NEGATE: {
                if (!IS_NUMBER(peek(vm, 0))) {
                    frame->ip = ip;
                    runtimeError(vm, "Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
                break;
            }
            
            case OP_PRINT: {
                printValue(pop(vm));
                printf("\n");
                break;
            }
            case OP_JUMP: {
                u16 offset = READ_SHORT();
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                u16 offset = READ_SHORT();
                if (!AS_BOOL(peek(vm, 0))) ip += offset;
                break;
            }
            case OP_LOOP: {
                u16 offset = READ_SHORT();
                ip -= offset;
                break;
            }
            case OP_CALL: {
                i32 argCount = READ_BYTE();
                frame->ip = ip;
                if (!callValue(vm, peek(vm, argCount), argCount)) return INTERPRET_RUNTIME_ERROR;
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                break;
            }
            case OP_RETURN: {
                Value result = pop(vm);
                vm->frameCount--;
                if (vm->frameCount == 0) {
                    pop(vm);
                    return INTERPRET_OK;
                }
                vm->stackTop = frame->slots;
                push(vm, result);
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                break;
            }
        }
    }
}

InterpretResult interpret(VM* vm, Chunk* chunk) {
    ObjFunction* function = newFunction(vm);
    // 拷贝代码所有权，确保 main.c 兼容
    function->chunk = *chunk; 
    push(vm, OBJ_VAL(function));
    call(vm, function, 0);
    return run(vm);
}
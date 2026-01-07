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
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", getLine(&function->chunk, (int)instruction));
        if (function->name == NULL) fprintf(stderr, "script\n");
        else fprintf(stderr, "%s()\n", function->name->chars);
    }
    resetStack(vm);
}
void initVM(VM* vm) {
    memset(vm, 0, sizeof(VM));
    resetStack(vm);
    vm->objects = NULL;
    initTable(&vm->globals);
    initTable(&vm->strings);
    vm->initString = copyString(vm, "init", 4);
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
    push(vm, OBJ_VAL(nameObj));
    ObjNative* nativeObj = newNative(vm, function);
    push(vm, OBJ_VAL(nativeObj));
    tableSet(vm, &vm->globals, OBJ_VAL(nameObj), OBJ_VAL(nativeObj));
    pop(vm);
    pop(vm);
}
// [新增] 捕获上值
static ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm->openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local) return upvalue;
    ObjUpvalue* createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) vm->openUpvalues = createdUpvalue;
    else prevUpvalue->next = createdUpvalue;
    return createdUpvalue;
}
// [新增] 关闭上值
static void closeUpvalues(VM* vm, Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}
static bool call(VM* vm, ObjClosure* closure, i32 argCount) {
    if (argCount != closure->function->arity) {
        runtimeError(vm, "Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }
    if (vm->frameCount == FRAMES_MAX) {
        runtimeError(vm, "Stack overflow.");
        return false;
    }
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure; // [修改]
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}
static bool callValue(VM* vm, Value callee, i32 argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm->stackTop[-argCount - 1] = bound->receiver;
                return callValue(vm, bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                vm->stackTop[-argCount - 1] = OBJ_VAL(newInstance(vm, klass));
                Value initializer;
                if (tableGet(&klass->methods, OBJ_VAL(vm->initString), &initializer)) {
                    return call(vm, AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError(vm, "Expected 0 arguments for initializer but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE: // [修改]
                return call(vm, AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(vm, argCount, vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }
            default: break;
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}
static bool bindMethod(VM* vm, ObjClass* klass, ObjString* name, Value receiver) {
    Value method;
    if (!tableGet(&klass->methods, OBJ_VAL(name), &method)) {
        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }
    ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
    push(vm, OBJ_VAL(bound));
    return true;
}
static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    register u8* ip = frame->ip;
    #define READ_BYTE() (*ip++)
    #define READ_SHORT() (ip += 2, (u16)((ip[-2] << 8) | ip[-1]))
    #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
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
        u8 instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: push(vm, READ_CONSTANT()); break;
            case OP_CONSTANT_LONG: {
                u32 index = READ_BYTE();
                index |= (u16)READ_BYTE() << 8;
                index |= (u32)READ_BYTE() << 16;
                push(vm, frame->closure->function->chunk.constants.values[index]);
                break;
            }
            case OP_NIL: push(vm, NIL_VAL); break;
            case OP_TRUE: push(vm, TRUE_VAL); break;
            case OP_FALSE: push(vm, FALSE_VAL); break;
            case OP_POP: pop(vm); break;
            case OP_GET_LOCAL: push(vm, frame->slots[READ_BYTE()]); break;
            case OP_SET_LOCAL: frame->slots[READ_BYTE()] = peek(vm, 0); break;
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm->globals, OBJ_VAL(name), &value)) {
                    frame->ip = ip;
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(vm, &vm->globals, OBJ_VAL(name), peek(vm, 0));
                pop(vm);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(vm, &vm->globals, OBJ_VAL(name), peek(vm, 0))) {
                    tableDelete(&vm->globals, OBJ_VAL(name));
                    frame->ip = ip;
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            // [新增] Upvalue 指令
            case OP_GET_UPVALUE: {
                u8 slot = READ_BYTE();
                push(vm, *frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                u8 slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(vm, 0);
                break;
            }
            case OP_CLOSE_UPVALUE: {
                closeUpvalues(vm, vm->stackTop - 1);
                pop(vm);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(vm, 0))) {
                    frame->ip = ip;
                    runtimeError(vm, "Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(pop(vm));
                ObjString* name = READ_STRING();
                Value value;
                if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
                    push(vm, value);
                    break;
                }
                Value receiver = OBJ_VAL(instance);
                if (!bindMethod(vm, instance->klass, name, receiver)) {
                    frame->ip = ip;
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(vm, 1))) {
                    frame->ip = ip;
                    runtimeError(vm, "Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(peek(vm, 1));
                tableSet(vm, &instance->fields, OBJ_VAL(READ_STRING()), peek(vm, 0));
                Value value = pop(vm);
                pop(vm);
                push(vm, value);
                break;
            }
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(pop(vm));
                Value receiver = pop(vm);
                if (!bindMethod(vm, superclass, name, receiver)) {
                    frame->ip = ip;
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: { Value b = pop(vm); Value a = pop(vm); push(vm, BOOL_VAL(valuesEqual(a, b))); break; }
            case OP_NOT_EQUAL: { Value b = pop(vm); Value a = pop(vm); push(vm, BOOL_VAL(!valuesEqual(a, b))); break; }
            case OP_GREATER: BINARY_BOOL_OP(>); break;
            case OP_GREATER_EQUAL: BINARY_BOOL_OP(>=); break;
            case OP_LESS: BINARY_BOOL_OP(<); break;
            case OP_LESS_EQUAL: BINARY_BOOL_OP(<=); break;
            case OP_ADD: BINARY_OP(NUMBER_VAL, +); break;
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
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
            case OP_PRINT: printValue(pop(vm)); printf("\n"); break;
            case OP_JUMP: { u16 offset = READ_SHORT(); ip += offset; break; }
            case OP_JUMP_IF_FALSE: { u16 offset = READ_SHORT(); if (!AS_BOOL(peek(vm, 0))) ip += offset; break; }
            case OP_LOOP: { u16 offset = READ_SHORT(); ip -= offset; break; }
            case OP_INVOKE: {
                ObjString* name = READ_STRING();
                i32 argCount = READ_BYTE();
                Value receiver = peek(vm, argCount);
                if (!IS_INSTANCE(receiver)) {
                    frame->ip = ip;
                    runtimeError(vm, "Only instances have methods.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(receiver);
                Value value;
                if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
                    vm->stackTop[-argCount - 1] = value;
                } else {
                    if (!tableGet(&instance->klass->methods, OBJ_VAL(name), &value)) {
                        frame->ip = ip;
                        runtimeError(vm, "Undefined property '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    ObjBoundMethod* bound = newBoundMethod(vm, receiver, value);
                    vm->stackTop[-argCount - 1] = OBJ_VAL(bound);
                }
                frame->ip = ip;
                if (!callValue(vm, vm->stackTop[-argCount - 1], argCount)) return INTERPRET_RUNTIME_ERROR;
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                break;
            }
            case OP_SUPER_INVOKE: {
                ObjString* name = READ_STRING();
                i32 argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(pop(vm));
               
                // [修复] 使用 peek 获取参数下方的 receiver，而不是 pop 掉最后一个参数
                Value receiver = peek(vm, argCount);
               
                Value method;
                if (!tableGet(&superclass->methods, OBJ_VAL(name), &method)) {
                    frame->ip = ip;
                    runtimeError(vm, "Undefined property '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
                vm->stackTop[-argCount - 1] = OBJ_VAL(bound);
               
                frame->ip = ip; // 记得保存 IP
                if (!callValue(vm, OBJ_VAL(bound), argCount)) return INTERPRET_RUNTIME_ERROR;
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
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
            case OP_BUILD_LIST: {
                u8 itemCount = READ_BYTE();
                ObjList* list = newList(vm);
                if (itemCount > 0) {
                    list->capacity = itemCount;
                    list->count = itemCount;
                    list->items = ALLOCATE(vm, Value, itemCount);
                    for (int i = itemCount - 1; i >= 0; i--) {
                        list->items[i] = pop(vm);
                    }
                }
                push(vm, OBJ_VAL(list));
                break;
            }
            case OP_BUILD_DICT: {
                u8 pairCount = READ_BYTE();
                ObjDict* dict = newDict(vm);
                for (u8 i = 0; i < pairCount; i++) {
                    Value value = pop(vm);
                    Value key = pop(vm);
                    tableSet(vm, &dict->items, key, value);
                }
                push(vm, OBJ_VAL(dict));
                break;
            }
            // [新增] 闭包指令
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(vm, function);
                push(vm, OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    u8 isLocal = READ_BYTE();
                    u8 index = READ_BYTE();
                    if (isLocal) closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
                    else closure->upvalues[i] = frame->closure->upvalues[index];
                }
                break;
            }
            case OP_CLASS: push(vm, OBJ_VAL(newClass(vm, READ_STRING()))); break;
            case OP_INHERIT: {
                Value superclass = peek(vm, 1);
                if (!IS_CLASS(superclass)) {
                    frame->ip = ip;
                    runtimeError(vm, "Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjClass* subclass = AS_CLASS(peek(vm, 0));
                tableAddAll(vm, &AS_CLASS(superclass)->methods, &subclass->methods);
                subclass->superclass = AS_CLASS(superclass);
                break;
            }
            case OP_METHOD: {
                Value method = peek(vm, 0);
                ObjClass* klass = AS_CLASS(peek(vm, 1));
                tableSet(vm, &klass->methods, OBJ_VAL(READ_STRING()), method);
                pop(vm);
                break;
            }
            case OP_RETURN: {
                Value result = pop(vm);
                closeUpvalues(vm, frame->slots); // [新增]
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
    function->chunk = *chunk;
    // [修改] 创建顶层 Closure
    ObjClosure* closure = newClosure(vm, function);
    push(vm, OBJ_VAL(closure));
    call(vm, closure, 0);
    return run(vm);
}
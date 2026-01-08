// src/core/vm/call_utils.c

#include "call_utils.h"
#include "error.h"
#include "core/memory.h"
#include "core/compiler/compiler.h" 

bool call(VM* vm, ObjClosure* closure, i32 argCount) {
    if (UNLIKELY(argCount != closure->function->arity)) {
        if (!runtimeError(vm, "Expected %d arguments but got %d.", closure->function->arity, argCount)) return false;
        return true; 
    }
  
    if (UNLIKELY(vm->frameCount == FRAMES_MAX)) {
        if (!runtimeError(vm, "Stack overflow.")) return false;
        return true;
    }
  
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}

bool bindKeywordArgs(VM* vm, ObjFunction* function, int argCount, int kwCount) {
    Value* kwBase = vm->stackTop - (kwCount * 2);
   
    for (int i = 0; i < kwCount; i++) {
        Value keyVal = kwBase[i * 2]; 
        
        if (!IS_STRING(keyVal)) {
            return runtimeError(vm, "Keyword keys must be strings.");
        }
        ObjString* name = AS_STRING(keyVal);
       
        int paramIndex = -1;
        for (int j = 0; j < function->arity; j++) {
            if (function->paramNames[j] == name ||
               (function->paramNames[j]->hash == name->hash &&
                memcmp(function->paramNames[j]->chars, name->chars, name->length) == 0)) {
                paramIndex = j;
                break;
            }
        }
       
        if (paramIndex == -1) {
            return runtimeError(vm, "Unexpected keyword argument '%s'.", name->chars);
        }
       
        if (paramIndex < argCount) {
            return runtimeError(vm, "Argument '%s' passed multiple times.", name->chars);
        }
    }
    return true;
}

bool callValue(VM* vm, Value callee, i32 argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE: {
                ObjClosure* closure = AS_CLOSURE(callee);
                ObjFunction* func = closure->function;
                if (argCount < func->minArity || argCount > func->arity) {
                    if(!runtimeError(vm, "Expected %d-%d arguments but got %d.",
                                        func->minArity, func->arity, argCount)) return false;
                    return true;
                }
                for (int i = argCount; i < func->arity; i++) {
                    push(vm, UNDEFINED_VAL);
                }
                return call(vm, closure, func->arity);
            }
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
                    return callValue(vm, initializer, argCount);
                } else if (argCount != 0) {
                    if(!runtimeError(vm, "Expected 0 arguments for initializer but got %d.", argCount)) return false;
                    return true;
                }
                return true;
            }
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
    if(!runtimeError(vm, "Can only call functions and classes.")) return false;
    return true;
}

bool bindMethod(VM* vm, ObjClass* klass, ObjString* name, Value receiver) {
    Value method;
    if (!tableGet(&klass->methods, OBJ_VAL(name), &method)) {
        if (!runtimeError(vm, "Undefined property '%s'.", name->chars)) return false;
        return true;
    }
  
    ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
    push(vm, OBJ_VAL(bound));
    return true;
}

bool prepareKeywordCall(VM* vm, ObjFunction* func, int argCount, int kwCount) {
    if (argCount > func->arity) {
        runtimeError(vm, "Expected at most %d arguments but got %d.", func->arity, argCount);
        return false;
    }
    Value* argsBase = vm->stackTop - (kwCount * 2) - argCount;
    Value* tempSlots = vm->stackTop;
    
    for (int i = 0; i < func->arity; i++) {
        tempSlots[i] = UNDEFINED_VAL;
    }
    for (int i = 0; i < argCount; i++) {
        tempSlots[i] = argsBase[i];
    }
    
    Value* kwBase = vm->stackTop - (kwCount * 2);
    for (int i = 0; i < kwCount; i++) {
        Value nameVal = kwBase[i * 2]; 
        Value valVal = kwBase[i * 2 + 1]; 
        ObjString* name = AS_STRING(nameVal);
       
        int paramIndex = -1;
        for (int j = 0; j < func->arity; j++) {
            if (func->paramNames[j] == name ||
               (func->paramNames[j]->hash == name->hash &&
                memcmp(func->paramNames[j]->chars, name->chars, name->length) == 0)) {
                paramIndex = j;
                break;
            }
        }
        if (paramIndex == -1) {
            runtimeError(vm, "Unexpected keyword argument '%s'.", name->chars);
            return false;
        }
        if (!IS_UNDEFINED(tempSlots[paramIndex])) {
            runtimeError(vm, "Argument '%s' passed multiple times.", name->chars);
            return false;
        }
        tempSlots[paramIndex] = valVal;
    }

    for (int i = 0; i < func->minArity; i++) {
        if (IS_UNDEFINED(tempSlots[i])) {
            runtimeError(vm, "Missing required argument '%s'.", func->paramNames[i]->chars);
            return false;
        }
    }

    for (int i = 0; i < func->arity; i++) {
        argsBase[i] = tempSlots[i];
    }
    vm->stackTop = argsBase + func->arity;
    return true;
}
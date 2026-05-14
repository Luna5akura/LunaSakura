// src/core/vm/call_utils.c

#include "call_utils.h"
#include "error.h"
#include "core/memory.h"
#include "core/compiler/compiler.h" 

static int findParamIndexFast(ObjFunction* function, ObjString* name) {
    if (!function) return -1;
    if (function->paramLookup && function->paramLookupCapacity > 0) {
        u32 mask = function->paramLookupCapacity - 1;
        u32 slot = name->hash & mask;
        for (;;) {
            ParamLookupEntry* entry = &function->paramLookup[slot];
            if (entry->key == NULL) return -1;
            if (entry->key == name) return (int)entry->index;
            slot = (slot + 1) & mask;
        }
    }

    for (int j = 0; j < function->arity; j++) {
        if (function->paramNames[j] == name ||
           (function->paramNames[j]->hash == name->hash &&
            memcmp(function->paramNames[j]->chars, name->chars, name->length) == 0)) {
            return j;
        }
    }
    return -1;
}

static int findNativeParamIndexFast(ObjNative* native, ObjString* name) {
    if (!native) return -1;
    if (native->paramLookup && native->paramLookupCapacity > 0) {
        u32 mask = native->paramLookupCapacity - 1;
        u32 slot = name->hash & mask;
        for (;;) {
            ParamLookupEntry* entry = &native->paramLookup[slot];
            if (entry->key == NULL) return -1;
            if (entry->key == name) return (int)entry->index;
            slot = (slot + 1) & mask;
        }
    }

    for (int j = 0; j < native->arity; j++) {
        if (native->paramNames[j] == name ||
           (native->paramNames[j]->hash == name->hash &&
            memcmp(native->paramNames[j]->chars, name->chars, name->length) == 0)) {
            return j;
        }
    }
    return -1;
}

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
       
        int paramIndex = findParamIndexFast(function, name);
       
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
                ObjNative* native = (ObjNative*)AS_OBJ(callee);
                if (native->arity >= 0 && (argCount < native->minArity || argCount > native->arity)) {
                    if (!runtimeError(vm, "Expected %d-%d arguments but got %d.",
                                      native->minArity, native->arity, argCount)) return false;
                    return true;
                }
                Value result = native->function(vm, argCount, vm->stackTop - argCount);
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
    if (klass->cachedMethodValid && klass->cachedMethodName == name) {
        method = klass->cachedMethodValue;
    } else if (tableGet(&klass->methods, OBJ_VAL(name), &method)) {
        klass->cachedMethodName = name;
        klass->cachedMethodValue = method;
        klass->cachedMethodValid = true;
    } else {
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
       
        int paramIndex = findParamIndexFast(func, name);
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

bool prepareKeywordNativeCall(VM* vm, ObjNative* native, int argCount, int kwCount) {
    Value* argsBase;
    Value* tempSlots;
    Value* kwBase;

    if (!native || native->arity < 0 || !native->paramNames) {
        runtimeError(vm, "Keyword arguments only supported for declared native functions.");
        return false;
    }
    if (argCount > native->arity) {
        runtimeError(vm, "Expected at most %d arguments but got %d.", native->arity, argCount);
        return false;
    }

    argsBase = vm->stackTop - (kwCount * 2) - argCount;
    tempSlots = vm->stackTop;
    for (int i = 0; i < native->arity; i++) {
        tempSlots[i] = UNDEFINED_VAL;
    }
    for (int i = 0; i < argCount; i++) {
        tempSlots[i] = argsBase[i];
    }

    kwBase = vm->stackTop - (kwCount * 2);
    for (int i = 0; i < kwCount; i++) {
        Value nameVal = kwBase[i * 2];
        Value valVal = kwBase[i * 2 + 1];
        ObjString* name = AS_STRING(nameVal);
        int paramIndex = findNativeParamIndexFast(native, name);
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

    for (int i = 0; i < native->minArity; i++) {
        if (IS_UNDEFINED(tempSlots[i])) {
            runtimeError(vm, "Missing required argument '%s'.", native->paramNames[i]->chars);
            return false;
        }
    }

    for (int i = 0; i < native->arity; i++) {
        argsBase[i] = tempSlots[i];
    }
    vm->stackTop = argsBase + native->arity;
    return true;
}

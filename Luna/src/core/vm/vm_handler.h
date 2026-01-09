// src/core/vm/vm_handler.h

#pragma once
#include "error.h"
#include "call_utils.h"
#include "core/memory.h"
// --- Macros for Handler Context ---
#define SP (*spPtr)
#define IP (*ipPtr)
#define FRAME (*framePtr)
#define PUSH(value) do { \
    if (UNLIKELY(SP >= vm->stack + STACK_MAX)) { \
        vm->stackTop = SP; \
        runtimeError(vm, "Stack overflow."); \
        return false; \
    } \
    *SP++ = (value); \
} while(0)
#define POP() (*(--SP))
#define PEEK(dist) (SP[-1 - (dist)])
#define DROP(n) (SP -= (n))
#define READ_BYTE() (*(IP++))
#define READ_SHORT() (IP += 2, (u16)((IP[-2] << 8) | IP[-1]))
#define READ_CONSTANT() (FRAME->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define RETURN_ERROR() return false
#define SYNC_VM() do { \
    vm->stackTop = SP; \
    FRAME->ip = IP; \
} while(0)
#define LOAD_FRAME() do { \
    FRAME = &vm->frames[vm->frameCount - 1]; \
    IP = FRAME->ip; \
} while(0)
// --- Opcodes Implementation ---
static INLINE bool invoke_binary_op(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr, ObjString* name) {
    Value a = PEEK(1); // Receiver
    
    if (!IS_INSTANCE(a)) return false;
    
    ObjInstance* instance = AS_INSTANCE(a);
    Value method;
    
    // 查找方法 (先在字段中找，再在类方法中找)
    if (tableGet(&instance->fields, OBJ_VAL(name), &method)) {
        // Found in fields
    } else if (tableGet(&instance->klass->methods, OBJ_VAL(name), &method)) {
        // Found in class
    } else {
        return false;
    }
    
    // 同步 VM 状态，因为 callValue 可能会触发 GC 或需要栈信息
    SYNC_VM();
    
    // 调用方法: Stack is [a, b]. 
    // callValue(method, 1) 会将 a 视为 receiver (this)
    if (!callValue(vm, method, 1)) RETURN_ERROR();
    
    // 恢复帧指针 (因为 callValue 可能推入了新帧或执行了 Native 代码)
    LOAD_FRAME();
    SP = vm->stackTop; 
    return true;
}
static INLINE bool op_constant(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value constant = READ_CONSTANT();
    PUSH(constant);
    return true;
}
static INLINE bool op_constant_long(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u32 index = READ_BYTE();
    index |= (u16)READ_BYTE() << 8;
    index |= (u32)READ_BYTE() << 16;
    PUSH(FRAME->closure->function->chunk.constants.values[index]);
    return true;
}
static INLINE bool op_get_local(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 slot = READ_BYTE();
    PUSH(FRAME->slots[slot]);
    return true;
}
static INLINE bool op_set_local(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 slot = READ_BYTE();
    FRAME->slots[slot] = PEEK(0);
    return true;
}
static INLINE bool op_get_global(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    Value value;
    if (!tableGet(&vm->globals, OBJ_VAL(name), &value)) {
        SYNC_VM();
        if (!runtimeError(vm, "Undefined variable '%s'.", name->chars)) RETURN_ERROR();
        LOAD_FRAME();
    } else {
        PUSH(value);
    }
    return true;
}
static INLINE bool op_define_global(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    tableSet(vm, &vm->globals, OBJ_VAL(name), PEEK(0));
    DROP(1);
    return true;
}
static INLINE bool op_set_global(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    if (tableSet(vm, &vm->globals, OBJ_VAL(name), PEEK(0))) {
        tableDelete(&vm->globals, OBJ_VAL(name));
        SYNC_VM();
        if (!runtimeError(vm, "Undefined variable '%s'.", name->chars)) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_get_upvalue(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 slot = READ_BYTE();
    PUSH(*FRAME->closure->upvalues[slot]->location);
    return true;
}
static INLINE bool op_set_upvalue(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 slot = READ_BYTE();
    *FRAME->closure->upvalues[slot]->location = PEEK(0);
    return true;
}
static INLINE bool op_close_upvalue(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    closeUpvalues(vm, SP - 1);
    DROP(1);
    return true;
}
static INLINE bool op_get_property(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    if (!IS_INSTANCE(PEEK(0))) {
        SYNC_VM();
        if (!runtimeError(vm, "Only instances have properties.")) RETURN_ERROR();
        LOAD_FRAME();
    } else {
        ObjInstance* instance = AS_INSTANCE(POP());
        ObjString* name = READ_STRING();
        Value value;
        if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
            PUSH(value);
        } else {
            SYNC_VM();
            if (!bindMethod(vm, instance->klass, name, OBJ_VAL(instance))) RETURN_ERROR();
            SP = vm->stackTop;
        }
    }
    return true;
}
static INLINE bool op_set_property(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    if (!IS_INSTANCE(PEEK(1))) {
        SYNC_VM();
        if (!runtimeError(vm, "Only instances have fields.")) RETURN_ERROR();
        LOAD_FRAME();
    } else {
        ObjInstance* instance = AS_INSTANCE(PEEK(1));
        tableSet(vm, &instance->fields, OBJ_VAL(READ_STRING()), PEEK(0));
        Value value = POP();
        DROP(1);
        PUSH(value);
    }
    return true;
}
static INLINE bool op_get_super(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    ObjClass* superclass = AS_CLASS(POP());
    Value receiver = POP();
    SYNC_VM();
    if (!bindMethod(vm, superclass, name, receiver)) {
        LOAD_FRAME();
        RETURN_ERROR();
    }
    SP = vm->stackTop;
    return true;
}
// Helper for op_add string concatenation
static bool concatenate_inline(VM* vm, Value** spPtr) {
    ObjString* b = AS_STRING(PEEK(0));
    ObjString* a = AS_STRING(PEEK(1));
    i32 length = a->length + b->length;
    char* chars = ALLOCATE(vm, char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
    ObjString* result = takeString(vm, chars, length);
    DROP(2);
    PUSH(OBJ_VAL(result));
    return true;
}
static INLINE bool op_add(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b));
    } else if (IS_STRING(a) && IS_STRING(b)) {
        SYNC_VM();
        if (!concatenate_inline(vm, spPtr)) RETURN_ERROR();
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opAddString)) {
        return true; // 重载调用成功
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be two numbers, two strings, or implement __add.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_subtract(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = NUMBER_VAL(AS_NUMBER(a) - AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opSubString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __sub.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_multiply(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = NUMBER_VAL(AS_NUMBER(a) * AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opMulString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __mul.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_divide(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = NUMBER_VAL(AS_NUMBER(a) / AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opDivString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __div.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_negate(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value a = PEEK(0);
    if (IS_NUMBER(a)) {
        SP[-1] = NUMBER_VAL(-AS_NUMBER(a));
    } else if (IS_INSTANCE(a)) {
        // 单目调用逻辑略有不同 (argCount = 0)
        ObjInstance* instance = AS_INSTANCE(a);
        Value method;
        if (tableGet(&instance->fields, OBJ_VAL(vm->opNegString), &method) ||
            tableGet(&instance->klass->methods, OBJ_VAL(vm->opNegString), &method)) {
            SYNC_VM();
            if (!callValue(vm, method, 0)) RETURN_ERROR();
            LOAD_FRAME();
            SP = vm->stackTop;
        } else {
             SYNC_VM();
             if (!runtimeError(vm, "Operand must be a number or implement __neg.")) RETURN_ERROR();
             LOAD_FRAME();
        }
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operand must be a number.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_greater(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = BOOL_VAL(AS_NUMBER(a) > AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opGtString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __gt.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_less(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = BOOL_VAL(AS_NUMBER(a) < AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opLtString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __lt.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_greater_equal(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = BOOL_VAL(AS_NUMBER(a) >= AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opGeString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __ge.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_less_equal(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value b = PEEK(0);
    Value a = PEEK(1);
    if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
        DROP(1);
        SP[-1] = BOOL_VAL(AS_NUMBER(a) <= AS_NUMBER(b));
    } else if (invoke_binary_op(vm, framePtr, spPtr, ipPtr, vm->opLeString)) {
        return true;
    } else {
        SYNC_VM();
        if (!runtimeError(vm, "Operands must be numbers or implement __le.")) RETURN_ERROR();
        LOAD_FRAME();
    }
    return true;
}
static INLINE bool op_call(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    i32 argCount = READ_BYTE();
    SYNC_VM();
    if (UNLIKELY(!callValue(vm, PEEK(argCount), argCount))) RETURN_ERROR();
    LOAD_FRAME();
    SP = vm->stackTop; // [关键修复] 同步栈指针
    return true;
}

static INLINE bool op_call_kw(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 argCount = READ_BYTE();
    u8 kwCount = READ_BYTE();
    SYNC_VM();
    
    // 计算被调用者位置：参数 + 关键字对 + 1
    Value callee = PEEK(argCount + kwCount * 2);
    ObjFunction* func = NULL;
    ObjClosure* closure = NULL;
  
    if (IS_CLOSURE(callee)) {
        closure = AS_CLOSURE(callee);
        func = closure->function;
    } else if (IS_BOUND_METHOD(callee)) {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        if (IS_CLOSURE(bound->method)) {
            closure = AS_CLOSURE(bound->method);
            func = closure->function;
        } else {
            if(!runtimeError(vm, "Keyword arguments only supported for declared functions.")) RETURN_ERROR();
            LOAD_FRAME();
            SP = vm->stackTop; // [安全同步]
            return true;
        }
    } else {
        if(!runtimeError(vm, "Keyword arguments only supported for declared functions.")) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // [安全同步]
        return true;
    }
  
    // 准备参数（重排栈）
    if (!prepareKeywordCall(vm, func, argCount, kwCount)) RETURN_ERROR();
  
    FRAME->ip = IP;
    if (!call(vm, closure, func->arity)) RETURN_ERROR();
    LOAD_FRAME();
    SP = vm->stackTop; // [关键修复] 同步栈指针
    return true;
}

static INLINE bool op_invoke(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    i32 argCount = READ_BYTE();
    Value receiver = PEEK(argCount);
    
    if (!IS_INSTANCE(receiver)) {
        SYNC_VM();
        if (!runtimeError(vm, "Only instances have methods.")) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // [安全同步]
    } else {
        ObjInstance* instance = AS_INSTANCE(receiver);
        Value value;
        // 1. Field
        if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
            SP[-argCount - 1] = value;
            SYNC_VM();
            if (!callValue(vm, value, argCount)) RETURN_ERROR();
        } else {
            // 2. Method
            if (!tableGet(&instance->klass->methods, OBJ_VAL(name), &value)) {
                SYNC_VM();
                if (!runtimeError(vm, "Undefined property '%s'.", name->chars)) RETURN_ERROR();
                LOAD_FRAME();
                SP = vm->stackTop; // [安全同步]
            } else {
                if (IS_CLOSURE(value) && argCount == AS_CLOSURE(value)->function->arity) {
                    SYNC_VM();
                    if (!call(vm, AS_CLOSURE(value), argCount)) RETURN_ERROR();
                } else {
                    ObjBoundMethod* bound = newBoundMethod(vm, receiver, value);
                    SP[-argCount - 1] = OBJ_VAL(bound);
                    SYNC_VM();
                    if (!callValue(vm, OBJ_VAL(bound), argCount)) RETURN_ERROR();
                }
            }
        }
        LOAD_FRAME();
        SP = vm->stackTop; // [关键修复] 同步栈指针
    }
    return true;
}

static INLINE bool op_invoke_kw(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    u8 argCount = READ_BYTE();
    u8 kwCount = READ_BYTE();
    SYNC_VM();
  
    Value* receiverPtr = vm->stackTop - (kwCount * 2) - argCount - 1;
    Value receiver = *receiverPtr;
  
    if (!IS_INSTANCE(receiver)) {
        if(!runtimeError(vm, "Only instances have methods.")) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // [安全同步]
    } else {
        ObjInstance* instance = AS_INSTANCE(receiver);
        Value value;
        bool found = false;
      
        if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
            *receiverPtr = value;
            if (!IS_CLOSURE(value)) {
                if(!runtimeError(vm, "Can only call functions.")) RETURN_ERROR();
                LOAD_FRAME(); 
                SP = vm->stackTop; // [安全同步]
                return true;
            }
            found = true;
        } else {
            if (!tableGet(&instance->klass->methods, OBJ_VAL(name), &value)) {
                if(!runtimeError(vm, "Undefined property '%s'.", name->chars)) RETURN_ERROR();
                LOAD_FRAME(); 
                SP = vm->stackTop; // [安全同步]
                return true;
            }
            if (!IS_CLOSURE(value)) {
                if(!runtimeError(vm, "Method must be a closure.")) RETURN_ERROR();
                LOAD_FRAME(); 
                SP = vm->stackTop; // [安全同步]
                return true;
            }
            ObjBoundMethod* bound = newBoundMethod(vm, receiver, value);
            *receiverPtr = OBJ_VAL(bound);
            found = true;
        }
        
        if (found) {
            ObjFunction* func = AS_CLOSURE(value)->function;
            if (!prepareKeywordCall(vm, func, argCount, kwCount)) RETURN_ERROR();
            FRAME->ip = IP;
            
            if (IS_CLOSURE(value)) {
                if (!call(vm, AS_CLOSURE(value), func->arity)) RETURN_ERROR();
            } else {
                if (!callValue(vm, *receiverPtr, func->arity)) RETURN_ERROR();
            }
        }
        LOAD_FRAME();
        SP = vm->stackTop; // [关键修复] 同步栈指针
    }
    return true;
}

static INLINE bool op_super_invoke(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    i32 argCount = READ_BYTE();
    ObjClass* superclass = AS_CLASS(POP());
    Value receiver = PEEK(argCount);
    Value method;
    
    if (!tableGet(&superclass->methods, OBJ_VAL(name), &method)) {
        SYNC_VM();
        if (!runtimeError(vm, "Undefined property '%s'.", name->chars)) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // [安全同步]
    } else {
        SYNC_VM();
        if (IS_CLOSURE(method)) {
            if (!call(vm, AS_CLOSURE(method), argCount)) RETURN_ERROR();
        } else {
            ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
            vm->stackTop[-argCount - 1] = OBJ_VAL(bound);
            if (!callValue(vm, OBJ_VAL(bound), argCount)) RETURN_ERROR();
        }
        LOAD_FRAME();
        SP = vm->stackTop; // [关键修复] 同步栈指针
    }
    return true;
}

static INLINE bool op_super_invoke_kw(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    u8 argCount = READ_BYTE();
    u8 kwCount = READ_BYTE();
    
    // [修正] 先弹出 superclass，本地 SP 更新
    ObjClass* superclass = AS_CLASS(POP());
    
    // [修正] 立即同步 vm->stackTop，使其反映 superclass 已被移除
    // 这样 prepareKeywordCall 里的指针计算才是正确的
    SYNC_VM();

    Value* receiverPtr = vm->stackTop - (kwCount * 2) - argCount - 1;
    Value receiver = *receiverPtr;
    Value method;
    
    if (!tableGet(&superclass->methods, OBJ_VAL(name), &method)) {
        if(!runtimeError(vm, "Undefined property '%s'.", name->chars)) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // [安全同步]
    } else if (!IS_CLOSURE(method)) {
        if(!runtimeError(vm, "Super method must be a closure.")) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // [安全同步]
    } else {
        ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
        *receiverPtr = OBJ_VAL(bound);
        
        ObjFunction* func = AS_CLOSURE(method)->function;
        if (!prepareKeywordCall(vm, func, argCount, kwCount)) RETURN_ERROR();
        
        FRAME->ip = IP;
        if (!callValue(vm, OBJ_VAL(bound), func->arity)) RETURN_ERROR();
        
        LOAD_FRAME();
        SP = vm->stackTop; // [关键修复] 同步栈指针
    }
    return true;
}
static INLINE bool op_build_list(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 itemCount = READ_BYTE();
    SYNC_VM();
    ObjList* list = newList(vm);
    if (itemCount > 0) {
        list->capacity = itemCount;
        list->count = itemCount;
        list->items = ALLOCATE(vm, Value, itemCount);
        for (int i = itemCount - 1; i >= 0; i--) {
            list->items[i] = *(--SP);
        }
        if (!isListHomogeneous(list)) {
            if (!runtimeError(vm, "List elements must be of the same type.")) RETURN_ERROR();
            LOAD_FRAME();
        }
    }
    PUSH(OBJ_VAL(list));
    return true;
}
static INLINE bool op_build_dict(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u8 pairCount = READ_BYTE();
    SYNC_VM();
    ObjDict* dict = newDict(vm);
    for (u8 i = 0; i < pairCount; i++) {
        Value value = POP();
        Value key = POP();
        tableSet(vm, &dict->items, key, value);
    }
    PUSH(OBJ_VAL(dict));
    return true;
}
static INLINE bool op_closure(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
    SYNC_VM();
    ObjClosure* closure = newClosure(vm, function);
    PUSH(OBJ_VAL(closure));
    for (int i = 0; i < closure->upvalueCount; i++) {
        u8 isLocal = READ_BYTE();
        u8 index = READ_BYTE();
        if (isLocal) {
            closure->upvalues[i] = captureUpvalue(vm, FRAME->slots + index);
        } else {
            closure->upvalues[i] = FRAME->closure->upvalues[index];
        }
    }
    return true;
}
static INLINE bool op_class(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    ObjString* name = READ_STRING();
    SYNC_VM();
    PUSH(OBJ_VAL(newClass(vm, name)));
    return true;
}
static INLINE bool op_inherit(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value superclass = PEEK(1);
    if (!IS_CLASS(superclass)) {
        SYNC_VM();
        if (!runtimeError(vm, "Superclass must be a class.")) RETURN_ERROR();
        LOAD_FRAME();
        SP = vm->stackTop; // 保持同步习惯
    } else {
        ObjClass* subclass = AS_CLASS(PEEK(0));
        tableAddAll(vm, &AS_CLASS(superclass)->methods, &subclass->methods);
        subclass->superclass = AS_CLASS(superclass);
        DROP(1); // [关键修复]：必须弹出子类，保持栈平衡！
    }
    return true;
}
static INLINE bool op_method(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value method = PEEK(0);
    ObjClass* klass = AS_CLASS(PEEK(1));
    tableSet(vm, &klass->methods, OBJ_VAL(READ_STRING()), method);
    POP();
    return true;
}
static INLINE bool op_return(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    Value result = POP();
    closeUpvalues(vm, FRAME->slots);
    vm->frameCount--;
    if (vm->frameCount == 0) {
        POP();
        return false; // Stop interpreter
    }
    SP = FRAME->slots;
    PUSH(result);
    LOAD_FRAME();
    return true;
}
static INLINE bool op_try(VM* vm, CallFrame** framePtr, Value** spPtr, u8** ipPtr) {
    u16 offset = READ_SHORT();
    Handler* handler = &vm->handlers[vm->handlerCount++];
    handler->frameIndex = vm->frameCount - 1;
    handler->handlerIp = IP + offset;
    handler->tryStackTop = SP;
    return true;
}
// Undefine macros
#undef SP
#undef IP
#undef FRAME
#undef PUSH
#undef POP
#undef PEEK
#undef DROP
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef SYNC_VM
#undef LOAD_FRAME
#undef RETURN_ERROR
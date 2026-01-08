// src/core/vm/vm.c

#include <stdarg.h>
#include <stdlib.h>
#include "error.h"
#include "call_utils.h"
#include "core/compiler/compiler.h"
#include "core/memory.h"
// --- Helper Functions ---
void closeUpvalues(VM* vm, Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}
// 非 static，供 vm_handler.h 使用
ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm->openUpvalues;
 
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
 
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
 
    ObjUpvalue* createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;
 
    if (prevUpvalue == NULL) {
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
 
    return createdUpvalue;
}
// --- Include Handlers ---
// 必须在 captureUpvalue 定义之后包含
#include "vm_handler.h"
// --- Lifecycle ---
void initVM(VM* vm) {
    memset(vm, 0, sizeof(VM));
    resetStack(vm);
 
    vm->objects = NULL;
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;
 
    initTable(&vm->globals);
    initTable(&vm->strings);
 
    vm->initString = NULL;
    vm->initString = copyString(vm, "init", 4);
 
    vm->active_timeline = NULL;
    vm->handlerCount = 0;
}
void freeVM(VM* vm) {
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->strings);
    vm->initString = NULL;
 
    vm->objects = NULL;
 
    if (vm->grayStack) {
        free(vm->grayStack);
        vm->grayStack = NULL;
    }
 
    if (vm->active_timeline) {
        timeline_free(vm, vm->active_timeline);
        vm->active_timeline = NULL;
    }
}
void defineNative(VM* vm, const char* name, NativeFn function) {
    ObjString* nameObj = copyString(vm, name, (i32)strlen(name));
    push(vm, OBJ_VAL(nameObj));
    push(vm, OBJ_VAL(newNative(vm, function)));
 
    tableSet(vm, &vm->globals, vm->stack[0], vm->stack[1]);
 
    pop(vm);
    pop(vm);
}
// --- Interpreter Loop ---
static InterpretResult run(VM* vm) {
    // [修复] 移除了 register 关键字
    // 因为我们需要在 switch 中对这些变量取地址 (&frame, &ip, &sp) 传给 handler 函数
    // 现代编译器在内联后会自动优化这些变量到寄存器，无需手动指定 register
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    u8* ip = frame->ip;
    Value* sp = vm->stackTop;
   
    // 为了简化调试，主循环使用手动宏定义 FETCH_BYTE
    // 这里只用于获取 instruction，具体的读取在 handler 中完成
    #define FETCH_BYTE() (*ip++)
    #define FETCH_SHORT() (ip += 2, (u16)((ip[-2] << 8) | ip[-1]))
    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        vm->stackTop = sp; // 同步 SP
       
        // [新增] 安全检查：防止 SP 越界导致打印循环失控
        if (sp < vm->stack || sp > vm->stack + STACK_MAX) {
            printf(" [CRITICAL] SP pointer corrupted: %p (Base: %p)\n", sp, vm->stack);
            // 可以选择在这里直接 return INTERPRET_RUNTIME_ERROR; 强行终止
        } else {
            printf(" "); // 稍微缩进一下，好看
            for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
        }
        disassembleInstruction(&frame->closure->function->chunk,
                               (int)(ip - frame->closure->function->chunk.code));
#endif
        u8 instruction = FETCH_BYTE();
       
        switch (instruction) {
            case OP_CONSTANT: op_constant(vm, &frame, &sp, &ip); break;
            case OP_CONSTANT_LONG: op_constant_long(vm, &frame, &sp, &ip); break;
            case OP_NIL: *sp++ = NIL_VAL; break;
            case OP_TRUE: *sp++ = TRUE_VAL; break;
            case OP_FALSE: *sp++ = FALSE_VAL; break;
            case OP_POP: sp--; break;
           
            case OP_GET_LOCAL: op_get_local(vm, &frame, &sp, &ip); break;
            case OP_SET_LOCAL: op_set_local(vm, &frame, &sp, &ip); break;
            case OP_GET_GLOBAL:
                if (!op_get_global(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_DEFINE_GLOBAL: op_define_global(vm, &frame, &sp, &ip); break;
            case OP_SET_GLOBAL:
                if (!op_set_global(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
           
            case OP_GET_UPVALUE: op_get_upvalue(vm, &frame, &sp, &ip); break;
            case OP_SET_UPVALUE: op_set_upvalue(vm, &frame, &sp, &ip); break;
            case OP_CLOSE_UPVALUE: op_close_upvalue(vm, &frame, &sp, &ip); break;
            case OP_GET_PROPERTY:
                if (!op_get_property(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_SET_PROPERTY:
                if (!op_set_property(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_GET_SUPER:
                if (!op_get_super(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_EQUAL: {
                Value b = *(--sp);
                Value a = *(--sp);
                *sp++ = BOOL_VAL(valuesEqual(a, b));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = *(--sp);
                Value a = *(--sp);
                *sp++ = BOOL_VAL(!valuesEqual(a, b));
                break;
            }
            case OP_GREATER:
                if (!op_greater(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_GREATER_EQUAL:
                if (!op_greater_equal(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_LESS:
                if (!op_less(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_LESS_EQUAL:
                if (!op_less_equal(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_ADD:
                if (!op_add(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_SUBTRACT:
                if (!op_subtract(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_MULTIPLY:
                if (!op_multiply(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_DIVIDE:
                if (!op_divide(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
           
            case OP_NOT: sp[-1] = BOOL_VAL(!AS_BOOL(sp[-1])); break;
            case OP_NEGATE:
                if (!IS_NUMBER(sp[-1])) {
                    vm->stackTop = sp; frame->ip = ip;
                    if (!runtimeError(vm, "Operand must be a number.")) return INTERPRET_RUNTIME_ERROR;
                    frame = &vm->frames[vm->frameCount - 1]; ip = frame->ip; sp = vm->stackTop;
                } else {
                    sp[-1] = NUMBER_VAL(-AS_NUMBER(sp[-1]));
                }
                break;
           
            case OP_PRINT:
                printValue(*(--sp));
                printf("\n");
                break;
           
            case OP_JUMP: {
                u16 offset = FETCH_SHORT();
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                u16 offset = FETCH_SHORT();
                if (!AS_BOOL(sp[-1])) ip += offset;
                break;
            }
            case OP_LOOP: {
                u16 offset = FETCH_SHORT();
                ip -= offset;
                break;
            }
           
            case OP_CALL:
                if (!op_call(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_CALL_KW:
                if (!op_call_kw(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_CHECK_DEFAULT: {
                u8 slot = FETCH_BYTE();
                u16 offset = FETCH_SHORT();
                if (!IS_UNDEFINED(frame->slots[slot])) ip += offset;
                break;
            }
            case OP_INVOKE:
                if (!op_invoke(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_INVOKE_KW:
                if (!op_invoke_kw(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_SUPER_INVOKE:
            case OP_SUPER_INVOKE_KW:
                if (instruction == OP_SUPER_INVOKE) {
                    if (!op_super_invoke(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                } else {
                    if (!op_super_invoke_kw(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                }
                break;
               
            case OP_BUILD_LIST:
                if (!op_build_list(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_BUILD_DICT: op_build_dict(vm, &frame, &sp, &ip); break;
           
            case OP_CLOSURE: op_closure(vm, &frame, &sp, &ip); break;
            case OP_CLASS: op_class(vm, &frame, &sp, &ip); break;
            case OP_INHERIT:
                if (!op_inherit(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR;
                break;
            case OP_METHOD: op_method(vm, &frame, &sp, &ip); break;
           
            case OP_RETURN:
                if (!op_return(vm, &frame, &sp, &ip)) return INTERPRET_OK;
                break;
           
            case OP_TRY: op_try(vm, &frame, &sp, &ip); break;
            case OP_POP_HANDLER: vm->handlerCount--; break;
        }
    }
   
    #undef FETCH_BYTE
    #undef FETCH_SHORT
}
InterpretResult interpret(VM* vm, Chunk* chunk) {
    ObjFunction* function = newFunction(vm);
    function->chunk = *chunk;
    initChunk(chunk);
 
    push(vm, OBJ_VAL(function));
    ObjClosure* closure = newClosure(vm, function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
 
    call(vm, closure, 0);
 
    return run(vm);
}

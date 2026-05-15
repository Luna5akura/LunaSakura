// src/core/vm/vm.c

#include <stdarg.h>
#include <stdlib.h>
#include "error.h"
#include "call_utils.h"
#include "core/compiler/compiler.h"
#include "core/memory.h"
#include "engine/model/animation.h"

extern bool syncBoundInstancePropertySet(VM* vm, ObjInstance* instance, ObjString* name, Value value,
                                         bool* out_handled, const char** out_error);
extern bool resolveBoundInstancePropertyGet(VM* vm, ObjInstance* instance, ObjString* name, Value* out_value,
                                            bool* out_handled);

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

    vm->opAddString = copyString(vm, "__add", 5);
    vm->opSubString = copyString(vm, "__sub", 5);
    vm->opMulString = copyString(vm, "__mul", 5);
    vm->opDivString = copyString(vm, "__div", 5);
    vm->opNegString = copyString(vm, "__neg", 5);
    vm->opLtString = copyString(vm, "__lt", 4);
    vm->opGtString = copyString(vm, "__gt", 4);
    vm->opLeString = copyString(vm, "__le", 4);
    vm->opGeString = copyString(vm, "__ge", 4);

    vm->user_data = NULL;
    vm->host_mark_roots = NULL;
    vm->handlerCount = 0;
    vm->captureReturn = false;
    vm->capturedReturn = NIL_VAL;
}
void freeVM(VM* vm) {
    reset_user_presets();
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->strings);
    vm->initString = NULL;
    freeObjects(vm);
    if (vm->grayStack) {
        free(vm->grayStack);
        vm->grayStack = NULL;
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
// --- Interpreter Loop (Optimized with Computed Goto) ---
static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    u8* ip = frame->ip;
    Value* sp = vm->stackTop;

    // Computed Goto 标签数组
    static void* dispatchTable[] = {
        &&OP_CONSTANT, &&OP_CONSTANT_LONG, &&OP_NIL, &&OP_TRUE, &&OP_FALSE,
        &&OP_POP, &&OP_GET_LOCAL, &&OP_SET_LOCAL, &&OP_GET_GLOBAL, &&OP_GET_GLOBAL_LONG,
        &&OP_SET_GLOBAL, &&OP_SET_GLOBAL_LONG, &&OP_DEFINE_GLOBAL, &&OP_DEFINE_GLOBAL_LONG,
        &&OP_GET_UPVALUE, &&OP_SET_UPVALUE, &&OP_EQUAL,
        &&OP_NOT_EQUAL, &&OP_GREATER, &&OP_GREATER_EQUAL, &&OP_LESS, &&OP_LESS_EQUAL,
        &&OP_ADD, &&OP_SUBTRACT, &&OP_MULTIPLY, &&OP_DIVIDE, &&OP_NOT, &&OP_NEGATE,
        &&OP_PRINT, &&OP_JUMP, &&OP_JUMP_IF_FALSE, &&OP_LOOP, &&OP_CALL, &&OP_CALL_KW,
        &&OP_CHECK_DEFAULT,&&OP_ITER_INIT, &&OP_ITER_NEXT,&&OP_LIST_APPEND,&&OP_BUILD_LIST, &&OP_BUILD_DICT, &&OP_CLOSURE, &&OP_CLOSURE_LONG,
        &&OP_CLOSE_UPVALUE, &&OP_RETURN, &&OP_CLASS, &&OP_CLASS_LONG, &&OP_INHERIT, &&OP_METHOD, &&OP_METHOD_LONG,
        &&OP_GET_PROPERTY, &&OP_GET_PROPERTY_LONG, &&OP_SET_PROPERTY, &&OP_SET_PROPERTY_LONG,
        &&OP_GET_SUPER, &&OP_GET_SUPER_LONG, &&OP_INVOKE, &&OP_INVOKE_LONG,
        &&OP_INVOKE_KW, &&OP_INVOKE_KW_LONG, &&OP_SUPER_INVOKE, &&OP_SUPER_INVOKE_LONG,
        &&OP_SUPER_INVOKE_KW, &&OP_SUPER_INVOKE_KW_LONG, &&OP_TRY,
        &&OP_POP_HANDLER, &&OP_UNKNOWN // 默认标签
    };

#define DISPATCH() goto *dispatchTable[*ip++]

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        vm->stackTop = sp;
        if (sp < vm->stack || sp > vm->stack + STACK_MAX) {
            printf(" [CRITICAL] SP pointer corrupted: %p (Base: %p)\n", sp, vm->stack);
        } else {
            printf(" ");
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

        DISPATCH();

        OP_CONSTANT: op_constant(vm, &frame, &sp, &ip); continue;
        OP_CONSTANT_LONG: op_constant_long(vm, &frame, &sp, &ip); continue;
        OP_NIL: *sp++ = NIL_VAL; continue;
        OP_TRUE: *sp++ = TRUE_VAL; continue;
        OP_FALSE: *sp++ = FALSE_VAL; continue;
        OP_POP: sp--; continue;
        OP_GET_LOCAL: op_get_local(vm, &frame, &sp, &ip); continue;
        OP_SET_LOCAL: op_set_local(vm, &frame, &sp, &ip); continue;
        OP_GET_GLOBAL: if (!op_get_global(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_GET_GLOBAL_LONG: if (!op_get_global_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SET_GLOBAL: if (!op_set_global(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SET_GLOBAL_LONG: if (!op_set_global_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_DEFINE_GLOBAL: op_define_global(vm, &frame, &sp, &ip); continue;
        OP_DEFINE_GLOBAL_LONG: op_define_global_long(vm, &frame, &sp, &ip); continue;
        OP_GET_UPVALUE: op_get_upvalue(vm, &frame, &sp, &ip); continue;
        OP_SET_UPVALUE: op_set_upvalue(vm, &frame, &sp, &ip); continue;
        OP_CLOSE_UPVALUE: op_close_upvalue(vm, &frame, &sp, &ip); continue;
        OP_GET_PROPERTY: if (!op_get_property(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_GET_PROPERTY_LONG: if (!op_get_property_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SET_PROPERTY: if (!op_set_property(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SET_PROPERTY_LONG: if (!op_set_property_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_GET_SUPER: if (!op_get_super(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_GET_SUPER_LONG: if (!op_get_super_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_EQUAL: { Value b = *(--sp); Value a = *(--sp); *sp++ = BOOL_VAL(valuesEqual(a, b)); continue; }
        OP_NOT_EQUAL: { Value b = *(--sp); Value a = *(--sp); *sp++ = BOOL_VAL(!valuesEqual(a, b)); continue; }
        OP_GREATER: if (!op_greater(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_GREATER_EQUAL: if (!op_greater_equal(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_LESS: if (!op_less(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_LESS_EQUAL: if (!op_less_equal(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_ADD: if (!op_add(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SUBTRACT: if (!op_subtract(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_MULTIPLY: if (!op_multiply(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_DIVIDE: if (!op_divide(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_NOT: sp[-1] = BOOL_VAL(!AS_BOOL(sp[-1])); continue;
        OP_NEGATE: if (!IS_NUMBER(sp[-1])) { vm->stackTop = sp; frame->ip = ip; if (!runtimeError(vm, "Operand must be a number.")) return INTERPRET_RUNTIME_ERROR; frame = &vm->frames[vm->frameCount - 1]; ip = frame->ip; sp = vm->stackTop; } else { sp[-1] = NUMBER_VAL(-AS_NUMBER(sp[-1])); } continue;
        OP_PRINT: printValue(*(--sp)); printf("\n"); continue;
        OP_JUMP: { u16 offset = (u16)((ip[0] << 8) | ip[1]); ip += 2 + offset; continue; }
        OP_JUMP_IF_FALSE: { u16 offset = (u16)((ip[0] << 8) | ip[1]); ip += 2; if (!AS_BOOL(sp[-1])) ip += offset; continue; }
        OP_LOOP: { u16 offset = (u16)((ip[0] << 8) | ip[1]); ip += 2; ip -= offset; continue; }
        OP_CALL: if (!op_call(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_CALL_KW: if (!op_call_kw(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_INVOKE: if (!op_invoke(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_INVOKE_LONG: if (!op_invoke_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_INVOKE_KW: if (!op_invoke_kw(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_INVOKE_KW_LONG: if (!op_invoke_kw_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SUPER_INVOKE: if (!op_super_invoke(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SUPER_INVOKE_LONG: if (!op_super_invoke_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SUPER_INVOKE_KW: if (!op_super_invoke_kw(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_SUPER_INVOKE_KW_LONG: if (!op_super_invoke_kw_long(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_CHECK_DEFAULT: continue;
        OP_ITER_INIT: if (!op_iter_init(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_ITER_NEXT: if (!op_iter_next(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_LIST_APPEND: if (!op_list_append(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_BUILD_LIST: if (!op_build_list(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_BUILD_DICT: op_build_dict(vm, &frame, &sp, &ip); continue;
        OP_CLOSURE: op_closure(vm, &frame, &sp, &ip); continue;
        OP_CLOSURE_LONG: op_closure_long(vm, &frame, &sp, &ip); continue;
        OP_CLASS: op_class(vm, &frame, &sp, &ip); continue;
        OP_CLASS_LONG: op_class_long(vm, &frame, &sp, &ip); continue;
        OP_INHERIT: if (!op_inherit(vm, &frame, &sp, &ip)) return INTERPRET_RUNTIME_ERROR; continue;
        OP_METHOD: op_method(vm, &frame, &sp, &ip); continue;
        OP_METHOD_LONG: op_method_long(vm, &frame, &sp, &ip); continue;
        OP_RETURN: if (!op_return(vm, &frame, &sp, &ip)) return INTERPRET_OK; continue;
        OP_TRY: op_try(vm, &frame, &sp, &ip); continue;
        OP_POP_HANDLER: vm->handlerCount--; continue;
        OP_UNKNOWN: runtimeError(vm, "Unknown opcode."); return INTERPRET_RUNTIME_ERROR;
    }
#undef DISPATCH
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

InterpretResult vm_call_value(VM* vm, Value callee, i32 argCount, Value* args, Value* out_result) {
    InterpretResult result;
    Value captured = NIL_VAL;
    if (!vm) return INTERPRET_RUNTIME_ERROR;
    if (vm->frameCount != 0) {
        if (!runtimeError(vm, "Nested VM callback invocation is not supported in the current state.")) {
            resetStack(vm);
        }
        return INTERPRET_RUNTIME_ERROR;
    }

    resetStack(vm);
    push(vm, callee);
    for (i32 i = 0; i < argCount; i++) {
        push(vm, args[i]);
    }
    if (!callValue(vm, callee, argCount)) {
        resetStack(vm);
        return INTERPRET_RUNTIME_ERROR;
    }

    if (vm->frameCount > 0) {
        vm->captureReturn = true;
        vm->capturedReturn = NIL_VAL;
        result = run(vm);
        captured = vm->capturedReturn;
        vm->captureReturn = false;
    } else {
        result = INTERPRET_OK;
        if (vm->stackTop > vm->stack) {
            captured = pop(vm);
        }
    }
    resetStack(vm);
    if (out_result) *out_result = captured;
    return result;
}

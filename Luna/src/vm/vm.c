// src/vm/vm.c

#include <stdarg.h>
#include <stdlib.h>
#include "compiler.h"
#include "memory.h"
#include "vm.h"
// --- Error Handling ---
static void closeUpvalues(VM* vm, Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm->openUpvalues;
        // 将值移动到堆上 (closed 字段)
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}

bool runtimeError(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Runtime Error: ");
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    for (i32 i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
       
        // 计算当前指令在 Chunk 中的偏移量
        size_t instruction = frame->ip - function->chunk.code - 1;
        i32 line = getLine(&function->chunk, (int)instruction);
       
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    if (vm->handlerCount == 0) {
        resetStack(vm);
        return false;
    }
    // Pop handler
    Handler handler = vm->handlers[--vm->handlerCount];
    // Unwind frames and close upvalues
    for (int i = vm->frameCount - 1; i > handler.frameIndex; i--) {
        CallFrame* unwindFrame = &vm->frames[i];
        closeUpvalues(vm, unwindFrame->slots);
    }
    vm->frameCount = handler.frameIndex + 1;
    // Refresh state
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    frame->ip = handler.handlerIp;
    vm->stackTop = handler.tryStackTop;
    closeUpvalues(vm, handler.tryStackTop);
    return true;
}
// --- Lifecycle ---
void initVM(VM* vm) {
    // 1. 内存清零
    memset(vm, 0, sizeof(VM));
   
    // 2. 初始化栈和帧
    resetStack(vm);
   
    // 3. 初始化全局状态
    vm->objects = NULL;
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024; // 初始 GC 阈值 1MB
   
    initTable(&vm->globals);
    initTable(&vm->strings);
   
    // 4. 创建内部字符串 (GC root)
    vm->initString = NULL; // 先置空防止 copyString 触发 GC 扫描野指针
    vm->initString = copyString(vm, "init", 4);
   
    vm->active_timeline = NULL;
    vm->handlerCount = 0;
}
void freeVM(VM* vm) {
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->strings);
    vm->initString = NULL;
   
    freeObjects(vm);
   
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
    // GC Safety: push 防止 tableSet 触发 GC 回收
    ObjString* nameObj = copyString(vm, name, (i32)strlen(name));
    push(vm, OBJ_VAL(nameObj));
    push(vm, OBJ_VAL(newNative(vm, function)));
   
    tableSet(vm, &vm->globals, vm->stack[0], vm->stack[1]);
   
    pop(vm);
    pop(vm);
}
// --- Helpers ---
static ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm->openUpvalues;
   
    // 查找是否已存在指向该 local 的 open upvalue
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
   
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
   
    // 创建新的 open upvalue
    ObjUpvalue* createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;
   
    if (prevUpvalue == NULL) {
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
   
    return createdUpvalue;
}

// 辅助：连接字符串
static void concatenate(VM* vm) {
    // 注意：此时 operand B 在栈顶，operand A 在次顶
    ObjString* b = AS_STRING(peek(vm, 0));
    ObjString* a = AS_STRING(peek(vm, 1));
   
    i32 length = a->length + b->length;
    // 分配新字符数组
    char* chars = ALLOCATE(vm, char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
   
    // takeString 接管 chars 的所有权
    ObjString* result = takeString(vm, chars, length);
   
    pop(vm); // Pop b
    pop(vm); // Pop a
    push(vm, OBJ_VAL(result));
}
// --- Invocation Logic ---
static bool call(VM* vm, ObjClosure* closure, i32 argCount) {
    if (UNLIKELY(argCount != closure->function->arity)) {
        if (runtimeError(vm, "Expected %d arguments but got %d.", closure->function->arity, argCount)) return true;
        return false;
    }
   
    if (UNLIKELY(vm->frameCount == FRAMES_MAX)) {
        if (runtimeError(vm, "Stack overflow.")) return true;
        return false;
    }
   
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}
static bool bindKeywordArgs(VM* vm, ObjFunction* function, int argCount, int kwCount) {
    // 栈布局: [ ... | arg0 | arg1 | key0 | val0 | key1 | val1 ]
    // key 是 ObjString*, val 是 Value
    // 我们需要把 val 填入 frame->slots 对应的位置
    
    // 关键字参数起始位置
    Value* kwBase = vm->stackTop - (kwCount * 2);
    
    // 1. 遍历栈上的关键字参数
    for (int i = 0; i < kwCount; i++) {
        Value keyVal = kwBase[i * 2]; // Name
        Value valVal = kwBase[i * 2 + 1]; // Value
        
        if (!IS_STRING(keyVal)) {
            return runtimeError(vm, "Keyword keys must be strings.");
        }
        ObjString* name = AS_STRING(keyVal);
        
        // 查找参数索引
        int paramIndex = -1;
        for (int j = 0; j < function->arity; j++) {
            // [优化] 字符串哈希比较
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
        
        // 检查是否已经通过位置参数传递
        if (paramIndex < argCount) {
            return runtimeError(vm, "Argument '%s' passed multiple times.", name->chars);
        }
        
        // 检查是否重复关键字 (VM 栈上的 slots 此时已被 argCount 填充，其余是垃圾值或上帧数据)
        // 实际上新帧的 slots 指向 arg0。
        // 我们需要小心：VM 的 call 逻辑是在 frame 创建后执行的。
        // 这里我们实际上在修改当前栈顶部分，随后 call 会将 stackTop 调整。
        
        // 目标 slot 相对于 arg0 的位置
        Value* slot = (vm->stackTop - argCount - (kwCount * 2)) + paramIndex;
        
        // 检查该 slot 是否已经被之前的关键字填充
        // 由于我们还未初始化 UNDEFINED，这里比较棘手。
        // 策略：我们先把所有位置参数保留，非位置参数初始化为 UNDEFINED，然后填充关键字。
    }
    return true;
}
static bool callValue(VM* vm, Value callee, i32 argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE: {
                ObjClosure* closure = AS_CLOSURE(callee);
                ObjFunction* func = closure->function;

                // 1. 检查参数数量是否在合法范围内 [minArity, arity]
                if (argCount < func->minArity || argCount > func->arity) {
                    return runtimeError(vm, "Expected %d-%d arguments but got %d.", 
                                        func->minArity, func->arity, argCount);
                }

                // 2. 填充缺省参数 (Padding)
                // 只有当传入参数少于总参数时才执行
                for (int i = argCount; i < func->arity; i++) {
                    push(vm, UNDEFINED_VAL);
                }

                // 3. 执行调用
                // 注意：此时栈上参数个数已补齐为 func->arity，所以传给 call 的是 arity
                return call(vm, closure, func->arity);
            }

            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                
                // 将栈深处的 receiver 占位符替换为真实的 bound->receiver
                // 此时 stackTop 还未移动（未填充 padding），receiver 在 stackTop - argCount - 1
                vm->stackTop[-argCount - 1] = bound->receiver;
                
                // 递归调用 bound->method
                // 这里的递归非常关键：它会进入 OBJ_CLOSURE 分支，从而复用上面的 Padding 逻辑
                return callValue(vm, bound->method, argCount);
            }

            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                
                // 1. 创建实例并替换栈上的 Class 对象
                vm->stackTop[-argCount - 1] = OBJ_VAL(newInstance(vm, klass));
                
                // 2. 查找并调用初始化器 (init)
                Value initializer;
                if (tableGet(&klass->methods, OBJ_VAL(vm->initString), &initializer)) {
                    // 递归调用 init 方法
                    // 同样利用递归来复用 Padding 逻辑，支持 init(a, b=1) 这种写法
                    return callValue(vm, initializer, argCount);
                } else if (argCount != 0) {
                    return runtimeError(vm, "Expected 0 arguments for initializer but got %d.", argCount);
                }
                return true;
            }

            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                // Native 函数通常不支持默认参数（除非在 Native 内部实现），直接透传
                Value result = native(vm, argCount, vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }

            default: break; // Fallthrough
        }
    }

    return runtimeError(vm, "Can only call functions and classes.");
}
static bool bindMethod(VM* vm, ObjClass* klass, ObjString* name, Value receiver) {
    Value method;
    if (!tableGet(&klass->methods, OBJ_VAL(name), &method)) {
        if (runtimeError(vm, "Undefined property '%s'.", name->chars)) return true;
        return false;
    }
   
    ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
    push(vm, OBJ_VAL(bound));
    return true;
}
static bool prepareKeywordCall(VM* vm, ObjFunction* func, int argCount, int kwCount) {
        // === 调试探针 START ===
    printf("DEBUG: prepareKeywordCall func=%p\n", (void*)func);
    printf("DEBUG: func->arity = %d\n", func->arity);
    printf("DEBUG: func->minArity = %d\n", func->minArity);
    printf("DEBUG: func->paramNames pointer = %p\n", (void*)func->paramNames);
    
    if (func->paramNames != NULL) {
        for (int i = 0; i < func->arity; i++) {
            printf("DEBUG: paramNames[%d] at %p = ", i, (void*)func->paramNames[i]);
            if (func->paramNames[i] != NULL) {
                // 尝试打印字符串内容，如果这里崩了，说明字符串指针是坏的
                printf("'%s'\n", func->paramNames[i]->chars); 
            } else {
                printf("NULL\n");
            }
        }
    } else {
        printf("DEBUG: paramNames is NULL! This will crash.\n");
    }
    fflush(stdout); // 确保崩溃前打印出来
    // 1. 检查位置参数是否超标
    if (argCount > func->arity) {
        runtimeError(vm, "Expected at most %d arguments but got %d.", func->arity, argCount);
        return false;
    }

    // 计算参数在栈上的起始位置 (callee 之后的第一个参数)
    // 栈布局: [callee/receiver] [arg0..N] [kwName0][kwVal0]...
    Value* argsBase = vm->stackTop - (kwCount * 2) - argCount;

    // 为了安全和简单，我们使用栈顶上方的未分配空间作为暂存区
    // 注意：假设 STACK_MAX 足够大，且暂存区不涉及 GC (因为只存 Value，且期间不分配对象)
    Value* tempSlots = vm->stackTop;

    // 2. 初始化暂存区：全部填为 UNDEFINED
    for (int i = 0; i < func->arity; i++) {
        tempSlots[i] = UNDEFINED_VAL;
    }

    // 3. 填充位置参数
    for (int i = 0; i < argCount; i++) {
        tempSlots[i] = argsBase[i];
    }

    // 4. 解析并填充关键字参数
    Value* kwBase = vm->stackTop - (kwCount * 2);
    for (int i = 0; i < kwCount; i++) {
        Value nameVal = kwBase[i * 2];     // Key
        Value valVal = kwBase[i * 2 + 1];  // Value

        // 这里的 Key 必定是 String (Compiler 保证)
        ObjString* name = AS_STRING(nameVal);
        
        // 在函数参数名列表中查找匹配项
        int paramIndex = -1;
        // [优化] 如果参数多，这里可以用 hash map，但一般函数参数少，线性扫描够快
        for (int j = 0; j < func->arity; j++) {
            // 指针相等检查 (String Interning 优势) || 内容检查
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

        // 检查重复赋值 (位置参数已填入 || 之前的关键字已填入)
        if (!IS_UNDEFINED(tempSlots[paramIndex])) {
            runtimeError(vm, "Argument '%s' passed multiple times.", name->chars);
            return false;
        }

        tempSlots[paramIndex] = valVal;
    }

    // 5. 检查必填参数 (minArity)
    for (int i = 0; i < func->minArity; i++) {
        if (IS_UNDEFINED(tempSlots[i])) {
            runtimeError(vm, "Missing required argument '%s'.", func->paramNames[i]->chars);
            return false;
        }
    }

    // 6. 重构栈 (In-place replace)
    // 将整理好的参数复制回 argsBase，覆盖掉原来的 args 和 kw_pairs
    for (int i = 0; i < func->arity; i++) {
        argsBase[i] = tempSlots[i];
    }

    // 7. 调整栈顶指针
    // 现在栈顶应该指向最后一个整理好的参数之后
    vm->stackTop = argsBase + func->arity;

    return true;
}
// --- Interpreter Loop (The Hot Path) ---
static InterpretResult run(VM* vm) {
    // 1. Cache VM State in Registers
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    register u8* ip = frame->ip;
   
    // [优化] 将 stackTop 缓存到局部变量，避免频繁的指针间接访问
    // 注意：任何可能触发 GC、调用函数或异常返回的地方，都必须同步 stackTop
    register Value* stackTop = vm->stackTop;
    // Macros using cached state
    #define READ_BYTE() (*ip++)
    #define READ_SHORT() (ip += 2, (u16)((ip[-2] << 8) | ip[-1]))
    #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
    #define READ_STRING() AS_STRING(READ_CONSTANT())
   
    // Stack macros (using local stackTop)
    #define PUSH(value) (*stackTop++ = (value))
    #define POP() (*(--stackTop))
    #define PEEK(dist) (stackTop[-1 - (dist)])
    #define DROP(n) (stackTop -= (n))
    #define BINARY_OP(valueType, op) \
        do { \
            Value b = PEEK(0); \
            Value a = PEEK(1); \
            if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) { \
                DROP(1); /* Pop b, reuse a's slot */ \
                /* 直接操作 stackTop[-1] 避免多余的内存写 */ \
                stackTop[-1] = valueType(AS_NUMBER(a) op AS_NUMBER(b)); \
            } else { \
                frame->ip = ip; \
                vm->stackTop = stackTop; \
                if (runtimeError(vm, "Operands must be numbers.")) continue; \
                else return INTERPRET_RUNTIME_ERROR; \
            } \
        } while (false)
    #define BINARY_BOOL_OP(op) \
        do { \
            Value b = PEEK(0); \
            Value a = PEEK(1); \
            if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) { \
                DROP(1); \
                stackTop[-1] = BOOL_VAL(AS_NUMBER(a) op AS_NUMBER(b)); \
            } else { \
                frame->ip = ip; \
                vm->stackTop = stackTop; \
                if (runtimeError(vm, "Operands must be numbers.")) continue; \
                else return INTERPRET_RUNTIME_ERROR; \
            } \
        } while (false)
    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        // Debug tracing needs sync
        vm->stackTop = stackTop;
        printf(" ");
        for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk,
                               (int)(ip - frame->closure->function->chunk.code));
#endif
        u8 instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                PUSH(constant);
                break;
            }
            case OP_CONSTANT_LONG: {
                u32 index = READ_BYTE();
                index |= (u16)READ_BYTE() << 8;
                index |= (u32)READ_BYTE() << 16;
                PUSH(frame->closure->function->chunk.constants.values[index]);
                break;
            }
            case OP_NIL: PUSH(NIL_VAL); break;
            case OP_TRUE: PUSH(TRUE_VAL); break;
            case OP_FALSE: PUSH(FALSE_VAL); break;
           
            case OP_POP: DROP(1); break;
           
            case OP_GET_LOCAL: {
                u8 slot = READ_BYTE();
                PUSH(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                u8 slot = READ_BYTE();
                frame->slots[slot] = PEEK(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm->globals, OBJ_VAL(name), &value)) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Undefined variable '%s'.", name->chars)) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                PUSH(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(vm, &vm->globals, OBJ_VAL(name), PEEK(0));
                DROP(1);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(vm, &vm->globals, OBJ_VAL(name), PEEK(0))) {
                    tableDelete(&vm->globals, OBJ_VAL(name));
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Undefined variable '%s'.", name->chars)) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                u8 slot = READ_BYTE();
                PUSH(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                u8 slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = PEEK(0);
                break;
            }
            case OP_CLOSE_UPVALUE: {
                closeUpvalues(vm, stackTop - 1);
                DROP(1);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(PEEK(0))) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Only instances have properties.")) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(POP());
                ObjString* name = READ_STRING();
               
                Value value;
                if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
                    PUSH(value);
                    break;
                }
               
                if (!bindMethod(vm, instance->klass, name, OBJ_VAL(instance))) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    return INTERPRET_RUNTIME_ERROR;
                }
                // bindMethod pushed the bound method, so stackTop needs implicit update?
                // No, bindMethod uses `push(vm, ...)`.
                // So we must sync local stackTop from vm->stackTop.
                stackTop = vm->stackTop;
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(PEEK(1))) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Only instances have fields.")) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(PEEK(1));
                tableSet(vm, &instance->fields, OBJ_VAL(READ_STRING()), PEEK(0));
                Value value = POP();
                DROP(1); // Pop instance
                PUSH(value);
                break;
            }
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(POP());
                Value receiver = POP();
               
                // Sync for bindMethod
                vm->stackTop = stackTop;
                if (!bindMethod(vm, superclass, name, receiver)) {
                    frame->ip = ip;
                    return INTERPRET_RUNTIME_ERROR;
                }
                stackTop = vm->stackTop;
                break;
            }
           
            case OP_EQUAL: {
                Value b = POP();
                Value a = POP();
                PUSH(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = POP();
                Value a = POP();
                PUSH(BOOL_VAL(!valuesEqual(a, b)));
                break;
            }
            case OP_GREATER: BINARY_BOOL_OP(>); break;
            case OP_GREATER_EQUAL: BINARY_BOOL_OP(>=); break;
            case OP_LESS: BINARY_BOOL_OP(<); break;
            case OP_LESS_EQUAL: BINARY_BOOL_OP(<=); break;
           
            case OP_ADD: {
                Value b = PEEK(0);
                Value a = PEEK(1);
               
                // [优化] 快速路径 1: 两个数字 (最常见)
                if (LIKELY(IS_NUMBER(a) && IS_NUMBER(b))) {
                    // 直接原地更新栈，减少 stackTop 移动指令
                    DROP(1);
                    stackTop[-1] = NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b));
                }
                // [优化] 快速路径 2: 两个字符串 (次常见)
                // 这里的 IS_STRING 展开后包含指针解引用，所以放在 IS_NUMBER 之后
                else if (IS_STRING(a) && IS_STRING(b)) {
                    vm->stackTop = stackTop; // Sync for GC safety
                    concatenate(vm);
                    stackTop = vm->stackTop;
                }
                // [优化] 慢速路径: 错误
                else {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Operands must be two numbers or two strings.")) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
           
            case OP_NOT:
                PUSH(BOOL_VAL(!AS_BOOL(POP())));
                break;
               
            case OP_NEGATE:
                if (!IS_NUMBER(PEEK(0))) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Operand must be a number.")) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                PUSH(NUMBER_VAL(-AS_NUMBER(POP())));
                break;
               
            case OP_PRINT: {
                printValue(POP());
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
                if (!AS_BOOL(PEEK(0))) ip += offset;
                break;
            }
            case OP_LOOP: {
                u16 offset = READ_SHORT();
                ip -= offset;
                break;
            }
           
            case OP_INVOKE: {
                ObjString* name = READ_STRING();
                i32 argCount = READ_BYTE();
                Value receiver = PEEK(argCount);
               
                if (!IS_INSTANCE(receiver)) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Only instances have methods.")) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
               
                ObjInstance* instance = AS_INSTANCE(receiver);
                Value value;
                // 1. 先检查字段
                if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
                    stackTop[-argCount - 1] = value;
                    frame->ip = ip;
                    vm->stackTop = stackTop; // Sync
                    if (!callValue(vm, value, argCount)) return INTERPRET_RUNTIME_ERROR;
                } else {
                    // 2. 检查方法
                    if (!tableGet(&instance->klass->methods, OBJ_VAL(name), &value)) {
                        frame->ip = ip;
                        vm->stackTop = stackTop;
                        if (runtimeError(vm, "Undefined property '%s'.", name->chars)) continue;
                        else return INTERPRET_RUNTIME_ERROR;
                    }
                   
                    // [优化] Fast Path: 如果是闭包，直接调用，避免分配 BoundMethod
                    if (IS_CLOSURE(value) && argCount == AS_CLOSURE(value)->function->arity) {
                        frame->ip = ip;
                        vm->stackTop = stackTop;
                        if (!call(vm, AS_CLOSURE(value), argCount)) return INTERPRET_RUNTIME_ERROR;
                    } else {
                        // Slow Path: 
                        // 1. 非闭包
                        // 2. 或者是闭包但参数不足（需要 callValue 处理 Padding）
                        ObjBoundMethod* bound = newBoundMethod(vm, receiver, value);
                        
                        // 将 BoundMethod 放入栈中原本 receiver 的位置
                        stackTop[-argCount - 1] = OBJ_VAL(bound);

                        frame->ip = ip;
                        vm->stackTop = stackTop;
                        
                        // 让 callValue 去处理参数检查和 Padding
                        if (!callValue(vm, OBJ_VAL(bound), argCount)) return INTERPRET_RUNTIME_ERROR;
                    }
                }
               
                // Refresh Frame
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                stackTop = vm->stackTop;
                break;
            }
           
            case OP_SUPER_INVOKE: {
                ObjString* name = READ_STRING();
                i32 argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(POP());
               
                Value receiver = PEEK(argCount);
                Value method;
               
                if (!tableGet(&superclass->methods, OBJ_VAL(name), &method)) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Undefined property '%s'.", name->chars)) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
               
                frame->ip = ip;
                vm->stackTop = stackTop; // Sync
               
                // 同上优化
                if (IS_CLOSURE(method)) {
                    if (!call(vm, AS_CLOSURE(method), argCount)) return INTERPRET_RUNTIME_ERROR;
                } else {
                    ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
                    vm->stackTop[-argCount - 1] = OBJ_VAL(bound);
                    if (!callValue(vm, OBJ_VAL(bound), argCount)) return INTERPRET_RUNTIME_ERROR;
                }
               
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                stackTop = vm->stackTop;
                break;
            }
            
            case OP_CHECK_DEFAULT: {
                u8 slot = READ_BYTE();
                u16 offset = READ_SHORT();
                
                // 如果参数不是 UNDEFINED，说明已经传递了值，跳过默认值计算代码
                if (!IS_UNDEFINED(frame->slots[slot])) {
                    ip += offset;
                }
                break;
            }

            case OP_CALL_KW: {
                u8 argCount = READ_BYTE();
                u8 kwCount = READ_BYTE();
                
                // [关键] 同步局部寄存器到 VM 结构体
                vm->stackTop = stackTop; 

                // 1. 获取被调用者 (位于所有参数之下)
                Value callee = PEEK(argCount + kwCount * 2);
                ObjFunction* func = NULL;
                ObjClosure* closure = NULL;

                // 2. 解析 Callee 类型
                if (IS_CLOSURE(callee)) {
                    closure = AS_CLOSURE(callee);
                    func = closure->function;
                } else if (IS_BOUND_METHOD(callee)) {
                    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                    if (IS_CLOSURE(bound->method)) {
                        closure = AS_CLOSURE(bound->method);
                        func = closure->function;
                    } else {
                        frame->ip = ip; // 报错前保存 IP 以便打印堆栈
                        runtimeError(vm, "Keyword arguments only supported for declared functions.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    frame->ip = ip;
                    runtimeError(vm, "Keyword arguments only supported for declared functions.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // 3. 执行参数标准化
                if (!prepareKeywordCall(vm, func, argCount, kwCount)) {
                    frame->ip = ip;
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                // 4. [关键] 保存当前 IP，防止死循环
                frame->ip = ip;

                // 5. 执行调用
                if (!call(vm, closure, func->arity)) return INTERPRET_RUNTIME_ERROR;

                // 6. 刷新 VM 寄存器
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                stackTop = vm->stackTop;
                break;
            }

            case OP_INVOKE_KW: {
                ObjString* name = READ_STRING();
                u8 argCount = READ_BYTE();
                u8 kwCount = READ_BYTE();
                
                // [关键] 同步栈指针
                vm->stackTop = stackTop;

                // 栈布局: [receiver] [args...] [kw_pairs...]
                Value* receiverPtr = stackTop - (kwCount * 2) - argCount - 1;
                Value receiver = *receiverPtr;

                if (!IS_INSTANCE(receiver)) {
                    frame->ip = ip;
                    runtimeError(vm, "Only instances have methods.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(receiver);

                Value value;
                // 1. 尝试查找字段 (Shadows methods)
                if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
                    *receiverPtr = value; // 将 receiver 替换为字段值
                    
                    if (!IS_CLOSURE(value)) {
                         frame->ip = ip;
                         runtimeError(vm, "Can only call functions.");
                         return INTERPRET_RUNTIME_ERROR;
                    }
                    ObjFunction* func = AS_CLOSURE(value)->function;
                    
                    if (!prepareKeywordCall(vm, func, argCount, kwCount)) {
                        frame->ip = ip;
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    // [关键] 保存 IP
                    frame->ip = ip;
                    
                    if (!call(vm, AS_CLOSURE(value), func->arity)) return INTERPRET_RUNTIME_ERROR;
                } 
                else {
                    // 2. 查找类方法
                    if (!tableGet(&instance->klass->methods, OBJ_VAL(name), &value)) {
                        frame->ip = ip;
                        runtimeError(vm, "Undefined property '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    if (!IS_CLOSURE(value)) {
                        frame->ip = ip;
                        runtimeError(vm, "Method must be a closure.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    ObjFunction* func = AS_CLOSURE(value)->function;

                    // 创建 BoundMethod 并替换 receiver
                    ObjBoundMethod* bound = newBoundMethod(vm, receiver, value);
                    *receiverPtr = OBJ_VAL(bound);

                    // 标准化参数
                    if (!prepareKeywordCall(vm, func, argCount, kwCount)) {
                        frame->ip = ip;
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    // [关键] 保存 IP
                    frame->ip = ip;
                    
                    // 调用 BoundMethod (使用 callValue 处理解包)
                    if (!callValue(vm, OBJ_VAL(bound), func->arity)) return INTERPRET_RUNTIME_ERROR;
                }

                // 刷新寄存器
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                stackTop = vm->stackTop;
                break;
            }

            case OP_SUPER_INVOKE_KW: {
                ObjString* name = READ_STRING();
                u8 argCount = READ_BYTE();
                u8 kwCount = READ_BYTE();

                // [关键] 同步栈指针
                vm->stackTop = stackTop;

                // 1. Pop superclass
                // 注意：POP() 宏修改的是局部变量 stackTop
                ObjClass* superclass = AS_CLASS(POP());
                
                // 再次同步，因为 POP 改变了局部 stackTop，而 prepareKeywordCall 依赖 vm->stackTop
                vm->stackTop = stackTop; 

                // 2. 定位 Receiver
                Value* receiverPtr = stackTop - (kwCount * 2) - argCount - 1;
                Value receiver = *receiverPtr;

                // 3. 查找方法
                Value method;
                if (!tableGet(&superclass->methods, OBJ_VAL(name), &method)) {
                    frame->ip = ip;
                    runtimeError(vm, "Undefined property '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (!IS_CLOSURE(method)) {
                    frame->ip = ip;
                    runtimeError(vm, "Super method must be a closure.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // 4. 绑定 this (receiver)
                ObjBoundMethod* bound = newBoundMethod(vm, receiver, method);
                *receiverPtr = OBJ_VAL(bound);

                // 5. 标准化参数
                ObjFunction* func = AS_CLOSURE(method)->function;
                if (!prepareKeywordCall(vm, func, argCount, kwCount)) {
                    frame->ip = ip;
                    return INTERPRET_RUNTIME_ERROR;
                }

                // [关键] 保存 IP
                frame->ip = ip;

                // 6. 调用
                if (!callValue(vm, OBJ_VAL(bound), func->arity)) return INTERPRET_RUNTIME_ERROR;

                // 刷新寄存器
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                stackTop = vm->stackTop;
                break;
            }
            case OP_CALL: {
                i32 argCount = READ_BYTE();
                frame->ip = ip;
                vm->stackTop = stackTop;
               
                // OP_CALL 的结果通常是成功的，runtimeError 在 callValue 内部处理
                // 这里没有什么特殊的位操作，但要注意保持 stackTop 同步
                if (UNLIKELY(!callValue(vm, PEEK(argCount), argCount))) {
                    return INTERPRET_RUNTIME_ERROR;
                }
               
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                stackTop = vm->stackTop;
                break;
            }
           
            case OP_BUILD_LIST: {
                u8 itemCount = READ_BYTE();
                // Allocating list triggers GC check, sync stack
                vm->stackTop = stackTop;
               
                ObjList* list = newList(vm);
                if (itemCount > 0) {
                    list->capacity = itemCount;
                    list->count = itemCount;
                    list->items = ALLOCATE(vm, Value, itemCount);
                   
                    // 从栈中复制元素 (注意 pop 顺序)
                    for (int i = itemCount - 1; i >= 0; i--) {
                        list->items[i] = POP();
                    }
                   
                    if (!isListHomogeneous(list)) {
                        frame->ip = ip;
                        vm->stackTop = stackTop; // Sync
                        if (runtimeError(vm, "List elements must be of the same type.")) continue;
                        else return INTERPRET_RUNTIME_ERROR;
                    }
                }
                PUSH(OBJ_VAL(list));
                break;
            }
           
            case OP_BUILD_DICT: {
                u8 pairCount = READ_BYTE();
                vm->stackTop = stackTop;
               
                ObjDict* dict = newDict(vm);
                for (u8 i = 0; i < pairCount; i++) {
                    Value value = POP();
                    Value key = POP();
                    tableSet(vm, &dict->items, key, value);
                }
                PUSH(OBJ_VAL(dict));
                break;
            }
           
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
               
                vm->stackTop = stackTop;
                ObjClosure* closure = newClosure(vm, function);
                PUSH(OBJ_VAL(closure));
               
                for (int i = 0; i < closure->upvalueCount; i++) {
                    u8 isLocal = READ_BYTE();
                    u8 index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
           
            case OP_CLASS: {
                ObjString* name = READ_STRING();
                vm->stackTop = stackTop;
                PUSH(OBJ_VAL(newClass(vm, name)));
                break;
            }
           
            case OP_INHERIT: {
                Value superclass = PEEK(1);
                if (!IS_CLASS(superclass)) {
                    frame->ip = ip;
                    vm->stackTop = stackTop;
                    if (runtimeError(vm, "Superclass must be a class.")) continue;
                    else return INTERPRET_RUNTIME_ERROR;
                }
                ObjClass* subclass = AS_CLASS(PEEK(0));
                // Copy methods from super to sub
                tableAddAll(vm, &AS_CLASS(superclass)->methods, &subclass->methods);
                subclass->superclass = AS_CLASS(superclass);
                POP(); // Pop subclass? No, typically INHERIT leaves subclass on stack or consumes?
                // Lox standard: OP_INHERIT doesn't pop, it just copies methods.
                // However, usually compiling 'class < sub' emits:
                // ... load super
                // ... load sub
                // ... inherit
                // ... pop (sub)
                break;
            }
           
            case OP_METHOD: {
                Value method = PEEK(0);
                ObjClass* klass = AS_CLASS(PEEK(1));
                tableSet(vm, &klass->methods, OBJ_VAL(READ_STRING()), method);
                POP(); // Pop method
                break;
            }
           
            case OP_RETURN: {
                Value result = POP();
                closeUpvalues(vm, frame->slots);
               
                vm->frameCount--;
                if (vm->frameCount == 0) {
                    POP(); // Pop <script> function/closure
                    return INTERPRET_OK;
                }
               
                stackTop = frame->slots; // Reset stack top to beginning of call
                PUSH(result);
               
                frame = &vm->frames[vm->frameCount - 1];
                ip = frame->ip;
                break;
            }
            case OP_TRY: {
                u16 offset = READ_SHORT();
                Handler* handler = &vm->handlers[vm->handlerCount++];
                handler->frameIndex = vm->frameCount - 1;
                handler->handlerIp = ip + offset;
                handler->tryStackTop = stackTop;
                break;
            }
            case OP_POP_HANDLER: {
                vm->handlerCount--;
                break;
            }
        }
    }
   
    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef PUSH
    #undef POP
    #undef PEEK
    #undef DROP
    #undef BINARY_OP
    #undef BINARY_BOOL_OP
}
InterpretResult interpret(VM* vm, Chunk* chunk) {
    ObjFunction* function = newFunction(vm);
   
    // [重要] 所有权转移 & 内存安全
    // 复制 chunk 内容到 function
    function->chunk = *chunk;
    // 将传入的临时 chunk 清空，防止调用者释放 array 导致 function 内部悬空
    // 假设调用者在 interpret 返回后会 check/free chunk
    initChunk(chunk);
   
    push(vm, OBJ_VAL(function)); // GC Safety
   
    ObjClosure* closure = newClosure(vm, function);
    pop(vm); // function
    push(vm, OBJ_VAL(closure));
   
    call(vm, closure, 0);
   
    return run(vm);
}
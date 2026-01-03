// src/vm/vm.h

#include <stdio.h>
#include <string.h>
#include "common.h"
#include "vm.h"
#include "chunk.h"
#include "object.h"

// 全局 VM 实例（简单起见，我们用全局变量）
VM vm;

void resetStack() {
    vm.stackTop = vm.stack;
}

// 在 initVM 附近添加
void defineNative(const char* name, NativeFn function) {
    // 把函数指针包装成 ObjNative
    // 把字符串包装成 ObjString
    // 存入 globals 哈希表
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    
    pop();
    pop();
}

// 核心：处理函数调用
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                // 此时栈顶是参数，argCount 决定了参数有多少个
                // stackTop - argCount 指向第一个参数的位置
                Value result = native(argCount, vm.stackTop - argCount);
                
                // 弹出参数 + 弹出函数对象本身
                vm.stackTop -= argCount + 1;
                
                // 压入返回值
                push(result);
                return true;
            }
            default:
                break; // 暂时不支持其他对象调用
        }
    }
    
    printf("Can only call functions and classes.\n");
    return false;
}

void initVM() {
    resetStack();
    initTable(&vm.globals);
    initTable(&vm.strings);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    // freeObjects();
}

void push(Value value) {
    *vm.stackTop = value; // 把值存入当前栈顶
    vm.stackTop++;        // 栈顶指针上移
}

Value pop() {
    vm.stackTop--;        // 栈顶指针下移
    return *vm.stackTop;  // 返回该处的值
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}


#define BINARY_OP(op) \
    do { \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(NUMBER_VAL(a op b)); \
    } while (false)

// 核心执行循环
static InterpretResult run() {
    // 辅助宏：读取当前指令字节，并将 IP 前移
    #define READ_BYTE() (*vm.ip++)
    
    // 辅助宏：读取常量池中的常量
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    #define READ_STRING() AS_STRING(vm.chunk->constants.values[READ_BYTE()])

    for (;;) {
        // 调试模式：打印栈的内容和当前指令
        #ifdef DEBUG_TRACE_EXECUTION
            printf("          ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            // 调用之前写的反汇编函数，打印当前正在执行的指令
            // (vm.ip - vm.chunk->code) 算出当前指令在数组中的偏移量
            disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
        #endif


        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant); // 把常量压入栈
                break;
            }
            case OP_NEGATE: {
                // 取出栈顶的值 -> 取负 -> 压回去
                // 注意：这里没有类型检查，如果对 "clip" 取负会崩，以后要加检查
                if (!IS_NUMBER(peek(0))) {
                    printf("Operand must be a number.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            }

            case OP_ADD:      BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE:   BINARY_OP(/); break;
            case OP_POP: {
                pop();
                break;
            }
            case OP_DEFINE_GLOBAL: {
                // 格式: [OP_DEFINE_GLOBAL] [StringIndex]
                ObjString* name = READ_STRING();
                // 值已经在栈顶了 (因为编译 var x = expr 时，先编译 expr)
                tableSet(&vm.globals, name, peek(0));
                pop(); // 定义完后，把值弹出栈
                break;
            }

            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    // 变量未定义，报错
                    printf("Undefined variable '%s'.\n", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value); // 把查到的值压入栈
                break;
            }

            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value = peek(0); // 取栈顶的值，但不弹出（为了支持 a = b = 1）
                
                // set 只能修改已存在的变量，不能创建新变量
                // 我们用一个小技巧：tableSet 如果返回 true 说明是新创建的
                if (tableSet(&vm.globals, name, value)) {
                    // 如果是新创建的，说明之前没有，那就是未定义！删除它并报错
                    tableDelete(&vm.globals, name); 
                    printf("Undefined variable '%s'.\n", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_CALL: {
                // 指令格式: [OP_CALL] [argCount]
                int argCount = READ_BYTE();
                
                // 栈的布局: [Function] [Arg1] [Arg2] ...
                // peek(argCount) 就是那个 Function 对象
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_RETURN: {
                // 打印栈顶的值作为程序结果
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }

    #undef READ_BYTE
    #undef READ_CONSTANT
}

InterpretResult interpret(Chunk* chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk->code; // IP 指向代码开始处
    return run();
}
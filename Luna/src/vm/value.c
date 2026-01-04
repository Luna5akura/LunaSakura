// src/vm/value.c

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "value.h"
#include "object.h"
#include "vm.h"  // 新增：为 extern VM vm

extern VM vm;  // 声明全局 vm

// --- 扩容逻辑 (Cold Path) ---
// 1. 跨平台兼容优化：增加 MSVC 支持
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void growValueArray(ValueArray* array) {
    u32 oldCapacity = array->capacity;
    u32 newCapacity = GROW_CAPACITY(oldCapacity);
   
    // 调用 memory.h 中的 reallocate
    // 注意：如果 reallocate 失败通常会直接 exit，所以不需要检查 NULL
    array->values = GROW_ARRAY(&vm, Value, array->values, oldCapacity, newCapacity);  // 传入 &vm
   
    array->capacity = newCapacity;
}

void freeValueArray(ValueArray* array) {
    // 释放内存
    // 假设 memory.h 中的 FREE_ARRAY 宏已经包含了指针置空逻辑
    FREE_ARRAY(&vm, Value, array->values, array->capacity);  // 传入 &vm
   
    // 重置元数据 (count = 0, capacity = 0)
    initValueArray(array);
}

void printValue(Value value) {
#ifdef NAN_BOXING
    // 2. 逻辑优化：按出现频率排序
    // Numbers 是最高频的，放在第一位判断
    if (IS_NUMBER(value)) {
        // 使用 %g 去掉尾随的 0 (例如 1.0000 -> 1)
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    } else if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    }
    // 3. 健壮性优化：处理未定义/损坏的值
    // 正常情况下不应到达这里，但这能救命（调试时）
    else {
        printf("<BAD VALUE: 0x%llx>", (unsigned long long)value);
    }
#else
    // Tagged Union Fallback
    switch (value.type) {
        case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: printObject(value); break;
        case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL: printf("nil"); break;
        default: printf("<UNKNOWN TYPE>"); break;
    }
#endif
}
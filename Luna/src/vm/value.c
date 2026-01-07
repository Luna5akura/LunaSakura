// src/vm/value.c

#include <stdio.h>
#include "memory.h"
#include "vm.h"
// --- 扩容逻辑 (Cold Path) ---
// [修改] 增加 VM* vm 参数
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void growValueArray(VM* vm, ValueArray* array) {
    u32 oldCapacity = array->capacity;
    u32 newCapacity = GROW_CAPACITY(oldCapacity);
    // [修改] 使用传入的 vm 调用内存分配器
    array->values = GROW_ARRAY(vm, Value, array->values, oldCapacity, newCapacity);
    array->capacity = newCapacity;
}
// [修改] 增加 VM* vm 参数
void freeValueArray(VM* vm, ValueArray* array) {
    // [修改] 使用传入的 vm 释放内存
    FREE_ARRAY(vm, Value, array->values, array->capacity);
    // 重置元数据
    initValueArray(array);
}
void printValue(Value value) {
#ifdef NAN_BOXING
    if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    } else if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    }
    else {
        printf("<BAD VALUE: 0x%llx>", (u64)value);
    }
#else
    switch (value.type) {
        case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: printObject(value); break;
        case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL: printf("nil"); break;
        default: printf("<UNKNOWN TYPE>"); break;
    }
#endif
}
// [新增] 值哈希函数
u32 valueHash(Value value) {
#ifdef NAN_BOXING
    if (IS_NUMBER(value)) {
        double num = AS_NUMBER(value);
        if (num != num) return 0; // NaN
        ValueConverter conv;
        conv.num = num;
        u64 bits = conv.bits;
        return (u32)(bits ^ (bits >> 32));
    } else if (IS_NIL(value)) {
        return 0;
    } else if (IS_BOOL(value)) {
        return AS_BOOL(value) ? 2166136261u : 16777619u;
    } else if (IS_OBJ(value)) {
        Obj* obj = AS_OBJ(value);
        switch (obj->type) {
            case OBJ_STRING:
                return ((ObjString*)obj)->hash;
            default:
                // 对于其他对象，使用指针哈希
                uintptr_t ptr = (uintptr_t)obj;
                return (u32)(ptr ^ (ptr >> 32));
        }
    }
    return 0;
#else
    // 非 NaN Boxing 的实现（如果需要）
    return 0;
#endif
}
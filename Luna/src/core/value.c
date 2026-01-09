// src/core/value.c

#include <inttypes.h> // [新增] 用于 PRIx64 宏，保证跨平台打印 u64 格式正确

#include "memory.h"
#include "vm/vm.h"

// --- 扩容逻辑 (Cold Path) ---

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void growValueArray(VM* vm, ValueArray* array) {
    u32 oldCapacity = array->capacity;
    u32 newCapacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(vm, Value, array->values, oldCapacity, newCapacity);
    array->capacity = newCapacity;
}

void freeValueArray(VM* vm, ValueArray* array) {
    FREE_ARRAY(vm, Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
#ifdef NAN_BOXING
    if (IS_OBJ(value)) {
        printObject(value);
    } else if (IS_NUMBER(value)) {
        printf("%.14g", AS_NUMBER(value));
    } else if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else {
        printf("<BAD VALUE: 0x%016" PRIx64 ">", value);
    }
#else
    switch (value.type) {
        case VAL_NUMBER: printf("%.14g", AS_NUMBER(value)); break;
        case VAL_OBJ:    printObject(value); break;
        case VAL_BOOL:   printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL:    printf("nil"); break;
        default:         printf("<UNKNOWN TYPE>"); break;
    }
#endif
}

u32 valueHash(Value value) {
#ifdef NAN_BOXING
    if (IS_OBJ(value)) {
        Obj* obj = AS_OBJ(value);
        if (obj->type == OBJ_STRING) {
            return ((ObjString*)obj)->hash;
        }
        u64 ptr = (u64)(uintptr_t)obj;
        ptr ^= ptr >> 33;
        ptr *= 0xff51afd7ed558ccdULL;
        ptr ^= ptr >> 33;
        ptr *= 0xc4ceb9fe1a85ec53ULL;
        ptr ^= ptr >> 33;
        return (u32)ptr;
    } 
    
    if (IS_NUMBER(value)) {
        double num = AS_NUMBER(value);
        if (num == 0) { 
            num = 0.0; 
        }
        u64 bits = numToValue(num);
        return (u32)(bits ^ (bits >> 32));
    } 
    
    if (IS_BOOL(value)) {
        return AS_BOOL(value) ? 3 : 5; 
    }
    
    return 0;
    
#else
    // Fallback implementation
    switch (value.type) {
        case VAL_BOOL:   return AS_BOOL(value) ? 3 : 5;
        case VAL_NIL:    return 0;
        case VAL_NUMBER: {
            double num = AS_NUMBER(value);
            if (num == 0) num = 0.0;
            u64 bits;
            memcpy(&bits, &num, sizeof(double));
            return (u32)(bits ^ (bits >> 32));
        }
        case VAL_OBJ: {
            // ... 同上 Obj 处理逻辑 ...
             Obj* obj = AS_OBJ(value);
             if (obj->type == OBJ_STRING) return ((ObjString*)obj)->hash;
             u64 ptr = (u64)(uintptr_t)obj;
             ptr ^= ptr >> 33;
             ptr *= 0xff51afd7ed558ccdULL;
             ptr ^= ptr >> 33;
             return (u32)ptr;
        }
        default: return 0;
    }
#endif
}
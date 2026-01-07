// src/vm/value.h

#pragma once
#include "common.h"

typedef struct VM VM;
// --- Forward Declarations ---
typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjClip ObjClip;
typedef struct sObjNative ObjNative;
typedef struct sObjTimeline ObjTimeline;
typedef struct sObjClass ObjClass; // [新增]
typedef struct sObjInstance ObjInstance; // [新增]
typedef struct sObjBoundMethod ObjBoundMethod; // [新增]
// --- NaN Boxing Configuration ---
#define NAN_BOXING
#ifdef NAN_BOXING
typedef u64 Value;
// ... (保留原有的宏定义 QNAN, SIGN_BIT, TAG_..., IS_...) ...
#define QNAN ((u64)0x7ffc000000000000)
#define SIGN_BIT ((u64)0x8000000000000000)
#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define IS_NUMBER(v) (((v) & QNAN) != QNAN)
#define IS_NIL(v) ((v) == NIL_VAL)
#define IS_BOOL(v) (((v) | 1) == TRUE_VAL)
#define IS_OBJ(v) (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define AS_NUMBER(v) valueToNum(v)
#define AS_BOOL(v) ((v) == TRUE_VAL)
#define AS_OBJ(v) ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))
#define NUMBER_VAL(num) numToValue(num)
#define NIL_VAL ((Value)(u64)(QNAN | TAG_NIL))
#define TRUE_VAL ((Value)(u64)(QNAN | TAG_TRUE))
#define FALSE_VAL ((Value)(u64)(QNAN | TAG_FALSE))
#define BOOL_VAL(b) ((Value)(FALSE_VAL | (!!(b))))
#define OBJ_VAL(obj) ((Value)(SIGN_BIT | QNAN | (u64)(uintptr_t)(obj)))
typedef union {
    double num;
    u64 bits;
} ValueConverter;
static INLINE Value numToValue(double num) {
    ValueConverter u;
    u.num = num;
    return u.bits;
}
static INLINE double valueToNum(Value value) {
    ValueConverter u;
    u.bits = value;
    return u.num;
}
static INLINE bool valuesEqual(Value a, Value b) {
    return a == b;
}
#else
// Fallback ...
#endif
// --- ValueArray ---
typedef struct {
    u32 capacity;
    u32 count;
    Value* values;
} ValueArray;
static INLINE void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}
// [修改] 声明增加 VM* vm
void growValueArray(VM* vm, ValueArray* array);
// [修改] 声明增加 VM* vm
static INLINE void writeValueArray(VM* vm, ValueArray* array, Value value) {
    if (UNLIKELY(array->count == array->capacity)) {
        growValueArray(vm, array);
    }
    array->values[array->count++] = value;
}
// [修改] 声明增加 VM* vm
void freeValueArray(VM* vm, ValueArray* array);
void printValue(Value value);
// [新增] 值哈希函数
u32 valueHash(Value value);
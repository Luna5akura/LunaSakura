// src/vm/value.h

#pragma once
#include "common.h"

// --- Forward Declarations ---
typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjClip ObjClip;
typedef struct sObjNative ObjNative;
typedef struct sObjTimeline ObjTimeline;

// --- NaN Boxing Configuration ---
// Note: Requires 48-bit pointers (Standard x64).
// Conflicts with Intel 5-level paging (57-bit address space).
#define NAN_BOXING

#ifdef NAN_BOXING
typedef u64 Value;
// Masks
#define QNAN ((u64)0x7ffc000000000000)
#define SIGN_BIT ((u64)0x8000000000000000)
// Tags
#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
// --- Type Checking ---
#define IS_NUMBER(v) (((v) & QNAN) != QNAN)
#define IS_NIL(v) ((v) == NIL_VAL)
#define IS_BOOL(v) (((v) | 1) == TRUE_VAL)
#define IS_OBJ(v) (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
// --- Value Extraction ---
#define AS_NUMBER(v) valueToNum(v)
#define AS_BOOL(v) ((v) == TRUE_VAL)
#define AS_OBJ(v) ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))
// --- Value Construction ---
#define NUMBER_VAL(num) numToValue(num)
#define NIL_VAL ((Value)(u64)(QNAN | TAG_NIL))
#define TRUE_VAL ((Value)(u64)(QNAN | TAG_TRUE))
#define FALSE_VAL ((Value)(u64)(QNAN | TAG_FALSE))
#define BOOL_VAL(b) ((Value)(FALSE_VAL | (!!(b))))
#define OBJ_VAL(obj) ((Value)(SIGN_BIT | QNAN | (u64)(uintptr_t)(obj)))
// --- Inline Helpers ---
// 使用 Union 进行类型双关，生成的汇编与 memcpy 一致，但调试更友好
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
    // Bitwise comparison handles NaN equality for VM internals
    return a == b;
}
#else
// Fallback (Tagged Union) implementation would go here
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

void growValueArray(ValueArray* array);

static INLINE void writeValueArray(ValueArray* array, Value value) {
    if (UNLIKELY(array->count == array->capacity)) {
        growValueArray(array);
    }
    array->values[array->count++] = value;
}

void freeValueArray(ValueArray* array);
void printValue(Value value);
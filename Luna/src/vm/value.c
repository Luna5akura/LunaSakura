// src/vm/value.c

#include <stdio.h>
#include <string.h>
#include <stdlib.h> // 暂时使用
// #include "src/vm/memory.h" // 稍后我们会用到内存管理，现在先假定用 realloc
#include "value.h"
#include "object.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = (oldCapacity < 8) ? 8 : oldCapacity * 2;
        // 这里为了简单直接用 realloc，正式版应该封装到 memory.c
        array->values = (Value*)realloc(array->values, sizeof(Value) * array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    free(array->values);
    initValueArray(array);
}

void printValue(Value value) {
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value); // 调用 object.c 的打印函数
    }
}
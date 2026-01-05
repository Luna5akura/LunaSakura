// src/vm/memory.h

#pragma once
#include "object.h"

// [新增] 前置声明 VM，防止循环依赖
typedef struct VM VM;

// --- Allocation Macros ---
#define ALLOCATE(vm, type, count) \
    (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, pointer) \
    reallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(vm, type, pointer, oldCount, newCount) \
    (type*)reallocate(vm, pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

#define FREE_ARRAY(vm, type, pointer, oldCount) do { \
    reallocate(vm, pointer, sizeof(type) * (oldCount), 0); \
    (pointer) = NULL; \
} while(0)

// --- Core Memory Manager ---
#if defined(__GNUC__) || defined(__clang__)
    #define ATTR_ALLOC __attribute__((malloc, alloc_size(4), warn_unused_result))
#else
    #define ATTR_ALLOC
#endif

void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) ATTR_ALLOC;

// --- Garbage Collection Interface ---
void collectGarbage(VM* vm);
void freeObject(VM* vm, Obj* object);
void freeObjects(VM* vm);

// [修改] 增加 VM* vm 参数，匹配 memory.c
void markObject(VM* vm, Obj* object);
void markValue(VM* vm, Value value);
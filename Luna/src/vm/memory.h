// src/vm/memory.h

#pragma once
#include "object.h"

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
// GCC/Clang 属性优化：告知编译器此函数类似 malloc (返回新指针, 且如果不检查返回值会有警告)
#if defined(__GNUC__) || defined(__clang__)
    #define ATTR_ALLOC __attribute__((malloc, alloc_size(4), warn_unused_result))
#else
    #define ATTR_ALLOC
#endif

void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) ATTR_ALLOC;

// --- Garbage Collection Interface ---

void collectGarbage(VM* vm);
void freeObjects(VM* vm);

// 仅在 .c 内部使用的慢路径函数，暴露出来给 inline 函数调用
void markObjectDo(VM* vm, Obj* object);

// [优化] Hot Path: 内联标记对象的快速检查
// 绝大多数对象在递归过程中已经被标记过，内联此检查可显著减少函数调用开销
static INLINE void markObject(VM* vm, Obj* object) {
    if (object == NULL) return;
    // 假设 isMarked 是 Obj 结构体的第一个或第二个成员，访问非常快
    if (object->isMarked) return;
    
    // 只有未标记的对象才进入慢路径 (推入 grayStack)
    markObjectDo(vm, object);
}

// [优化] Hot Path: 内联值标记
static INLINE void markValue(VM* vm, Value value) {
    if (IS_OBJ(value)) {
        markObject(vm, AS_OBJ(value));
    }
}

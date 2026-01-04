// src/vm/memory.h

#pragma once // 1. 编译优化
#include "common.h"
#include "object.h"
#include "vm.h"  // 新增：定义 VM 类型

// --- Allocation Macros ---
// Allocates memory for a single object of 'type'.
// cast to (type*) is useful for C++ compatibility if you compile as C++.
#define ALLOCATE(vm, type, count) \
    (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))
#define FREE(vm, type, pointer) \
    reallocate(vm, pointer, sizeof(type), 0)

// --- Dynamic Array Macros ---
// 3. 健壮性优化：虽然极少发生，但生产级代码应考虑 capacity 溢出
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)
// Resizes a dynamic array.
#define GROW_ARRAY(vm, type, pointer, oldCount, newCount) \
    (type*)reallocate(vm, pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))
// 同 FREE，增加置空保护
#define FREE_ARRAY(vm, type, pointer, oldCount) do { \
    reallocate(vm, pointer, sizeof(type) * (oldCount), 0); \
    (pointer) = NULL; \
} while(0)

// --- Core Memory Manager ---
// 4. 编译器优化提示 (GCC/Clang)
// - malloc: 返回的指针不与现有指针重叠 (No Aliasing)
// - alloc_size(3): 第3个参数 (newSize) 是分配字节数，利于静态分析
// - warn_unused_result: 警告调用者必须处理返回值 (防止 realloc 移动内存后原指针失效)
#if defined(__GNUC__) || defined(__clang__)
    #define ATTR_ALLOC __attribute__((malloc, alloc_size(4), warn_unused_result))
#else
    #define ATTR_ALLOC
#endif
// The single bottleneck for all memory operations.
// 1. newSize > 0, oldSize == 0: Allocate (malloc)
// 2. newSize == 0, oldSize > 0: Free (free)
// 3. newSize > oldSize: Grow (realloc)
// 4. newSize < oldSize: Shrink (realloc)
void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) ATTR_ALLOC;

// --- Garbage Collection Interface ---
// Triggers a collection cycle (Mark & Sweep).
// 通常更名为 collectGarbage 以区别于 Teardown 时的 freeObjects
void collectGarbage(VM* vm);
// Teardown: Frees ALL objects regardless of references (VM shutdown).
void freeObject(VM* vm, Obj* object);  // Declaration for external use
void freeObjects(VM* vm);
// 5. 补充 GC 标记接口
// 编译器和 VM 栈在 GC 阶段需要标记它们引用的对象为“存活”。
void markObject(Obj* object);
void markValue(Value value);

// src/allocator.h

#pragma once
#include <stddef.h>

// 定义通用的内存分配函数指针类型
// ctx: 上下文（在运行时指向 VM）
// ptr: 旧指针（对于 malloc 为 NULL）
// old_size: 旧大小（对于 malloc 为 0）
// new_size: 新大小（对于 free 为 0）
typedef void* (*ReallocFn)(void* ctx, void* ptr, size_t old_size, size_t new_size);

// 分配器结构体
typedef struct {
    ReallocFn fn;
    void* ctx; 
} Allocator;

// --- Model 层专用的辅助宏 (类似于 core/memory.h 但不依赖 VM) ---

#define MEM_ALLOC(allocator, type, count) \
    (type*)((allocator)->fn((allocator)->ctx, NULL, 0, sizeof(type) * (count)))

#define MEM_FREE(allocator, type, pointer) \
    ((allocator)->fn((allocator)->ctx, pointer, sizeof(type), 0))

#define MEM_GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

// 注意：这里需要传入 oldCount 以便 VM 统计内存差值
#define MEM_GROW_ARRAY(allocator, type, pointer, oldCount, newCount) \
    (type*)((allocator)->fn((allocator)->ctx, pointer, \
        sizeof(type) * (oldCount), \
        sizeof(type) * (newCount)))

#define MEM_FREE_ARRAY(allocator, type, pointer, oldCount) \
    ((allocator)->fn((allocator)->ctx, pointer, sizeof(type) * (oldCount), 0))
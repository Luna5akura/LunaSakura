// src/vm/memory.h

#ifndef LUNA_MEMORY_H
#define LUNA_MEMORY_H

#include "common.h"
#include "object.h"

// 重新分配内存大小
// oldSize: 0 -> malloc
// newSize: 0 -> free
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// 释放所有对象（在程序结束时调用）
void freeObjects();

#endif
// mem.h

#ifndef MEM_H
#define MEM_H

#include "common.h"

void* mmalloc(unsigned int size);
void* mmrealloc(void* ptr, size_t new_size);
void mfree(void *ptr);
void* mcopy(void* dest, const void* src, size_t num);
void* mset(void* prt, int value, size_t num);
void minit();
void mreset();

#endif
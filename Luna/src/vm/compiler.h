// src/vm/compiler.h

#ifndef LUNA_COMPILER_H
#define LUNA_COMPILER_H

#include "vm.h"

// 将源代码编译进 Chunk
// 如果编译成功返回 true，否则 false
bool compile(const char* source, Chunk* chunk);

#endif
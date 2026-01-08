// src/core/compiler/compiler.h

#pragma once
#include "core/vm/vm.h"
// Compiles source code into a Bytecode Chunk.
// Returns true if compilation succeeded.
// [注意] Chunk 所有权转移给调用者，需手动 free
bool compile(VM* vm, const char* source, Chunk* chunk);
// GC Helper: 标记编译器正在使用的对象（如函数名、常量等），防止编译期间 GC 回收
void markCompilerRoots(VM* vm);
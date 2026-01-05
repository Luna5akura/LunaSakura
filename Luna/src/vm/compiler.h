// src/vm/compiler.h

#ifndef LUNA_COMPILER_H
#define LUNA_COMPILER_H

#include "chunk.h"

// Compiles source code into a Bytecode Chunk.
// Returns true if compilation succeeded.
bool compile(VM* vm, const char* source, Chunk* chunk);

#endif
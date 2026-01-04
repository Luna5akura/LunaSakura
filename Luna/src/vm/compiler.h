// src/vm/compiler.h

#ifndef LUNA_COMPILER_H
#define LUNA_COMPILER_H

#include <stdbool.h>
#include "chunk.h"

// Compiles source code into a Bytecode Chunk.
// Returns true if compilation succeeded.
bool compile(const char* source, Chunk* chunk);

#endif
// compiler.h

#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "chunk.h"

typedef struct {
  Chunk* chunk;
  // ObjFunction* current_function;
} Compiler;

void init_compiler(Compiler* compiler);
void free_compiler(Compiler* compiler);

void compile(Node* node, Compiler* compiler);

#endif
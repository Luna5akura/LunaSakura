// chunk.h

#ifndef CHUNK_H
#define CHUNK_H

#include "common.h"
#include "value.h"

typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  size_t* lines;
  ValueArray constants;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte, size_t line);

int add_constant(Chunk* chunk, Value value);
int write_jump(Chunk* chunk, uint8_t opcode, int line);
void patch_jump(Chunk* chunk, int offset);


#endif

//chunk.c

#include "stdio.h"
#include "common.h"
#include "mem.h"

#include "value.h"

#include "chunk.h"


void init_chunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = nullptr;
  chunk->lines = nullptr;
  init_value_array(&chunk->constants);
}

void free_chunk(Chunk* chunk) {
  mfree(chunk->code);
  mfree(chunk->lines);
  free_value_array(&chunk->constants);
  init_chunk(chunk);
}

void write_chunk(Chunk* chunk, uint8_t byte, size_t line) {
  if (chunk->capacity < chunk->count + 1) {
    int old_capacity = chunk->capacity;
    chunk->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    chunk->code = mmrealloc(chunk->code, chunk->capacity);
    chunk->lines = mmrealloc(chunk->lines, chunk->capacity * sizeof(int));
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

int add_constant(Chunk* chunk, Value value) {
  write_value_array(&chunk->constants, value);
  return chunk->constants.count - 1;
}

int write_jump(Chunk* chunk, uint8_t opcode, int line) {
  write_chunk(chunk, opcode, line);
  write_chunk(chunk, 0xff, line);
  write_chunk(chunk, 0xff, line);
  return chunk->count - 2;
}

void patch_jump(Chunk* chunk, int offset) {
  int jump = chunk->count - offset - 2;
  if (jump > UINT16_MAX) {
    pprintf("Too much code to jump over.\n");
    // TODO: exit
  }
  chunk->code[offset] = (jump >> 8) & 0xff;
  chunk->code[offset + 1] = jump & 0xff;
}
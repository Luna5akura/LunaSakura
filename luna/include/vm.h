// vm.h

#ifndef VM_H
#define VM_H


#include "chunk.h"
#include "object.h"
#include "environment.h"

#define STACK_MAX 256
#define FRAMES_MAX 64

typedef struct {
  ObjFunction* function;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  Chunk* chunk;
  uint8_t* ip;

  Value stack[STACK_MAX];
  Value* stack_top;

  CallFrame frames[FRAMES_MAX];
  int frame_count;

  Environment* environment;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(VM* vm);
void free_vm(VM* vm);

InterpretResult interpret(VM* vm, Chunk* chunk);

void push(VM* vm, Value value);
Value pop(VM* vm);

#endif

// builtin.h

#ifndef BUILTIN_H
#define BUILTIN_H

#include "value.h"
#include "vm.h"

typedef struct {
  char* chars;
  int length;
  int capacity;
} DynamicString;

void init_dynamic_string(DynamicString* ds);
void append_char_to_dynamic_string(DynamicString* ds, char c);
void free_dynamic_string(DynamicString* ds);
void list_add(ObjList* list, Value value);
Value native_print(VM* vm, int arg_count, Value* args);
Value native_range(VM* vm, int arg_count, Value* args);
Value native_input(VM* vm, int arg_count, Value* args);
Value iterator_next(ObjIterator* iterator);

#endif

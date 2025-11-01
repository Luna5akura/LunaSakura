// object.h

#ifndef OBJECT_H
#define OBJECT_H

#include "value.h"
#include "chunk.h"

struct VM;

typedef enum {
  OBJ_NUMBER,
  OBJ_STRING,
  OBJ_BOOL,
  OBJ_LIST,
  OBJ_NONE,
  OBJ_NATIVE,
  OBJ_FUNCTION,
  OBJ_RANGE,
  OBJ_ITERATOR,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
    ObjList* list;
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
};

struct ObjList {
    Obj obj;
    ValueArray content;
};

struct ObjFunction {
    Obj obj;
    int arity;
    Chunk chunk;
    ObjString* name;
    char** arg_names;
};

struct ObjIterator {
    Obj obj;
    Obj* iterable;
    int current_index;
};

struct ObjRange {
    Obj obj;
    int start;
    int end;
    int step;
    int current;
};

typedef Value (*NativeFn)(struct VM* vm, int arg_count, Value* args);
typedef Value (*Fn)(struct VM* vm, int arg_count, Value* args);

struct ObjNative {
    Obj obj;
    NativeFn function;
};

ObjString* copy_string(const char* chars, int length);
ObjNative* new_native(NativeFn function);
ObjFunction* new_function();
ObjList* new_list();
ObjIterator* new_iterator(Obj* iterable);
ObjRange* new_range(int start, int end, int step);

void print_object(Obj* object);

#endif

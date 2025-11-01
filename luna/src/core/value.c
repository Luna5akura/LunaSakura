// value.c

#include "stdio.h"
#include "mem.h"

#include "object.h"

#include "value.h"

void init_value_array(ValueArray* array) {
  array->count = 0;
  array->capacity = 0;
  array->values = nullptr;
}

void write_value_array(ValueArray* array, Value value) {
  if (array->capacity < array->count + 1) {
    int old_capacity = array->capacity;
    array->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    array->values = mmrealloc(array->values, sizeof(Value) * array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void free_value_array(ValueArray* array) {
  mfree(array->values);
  init_value_array(array);
}

void print_value(Value value) {
    switch (value.type) {
        case VAL_BOOL:
            pprintf(AS_BOOL(value) ? "True" : "False");
            break;
        case VAL_NIL:
            pprintf("None");
            break;
        case VAL_NUMBER:
            size_t num = AS_NUMBER(value);
            pprintf("%d", num);
            break;
        case VAL_OBJ:
            print_object(AS_OBJ(value));
            break;
    }
}

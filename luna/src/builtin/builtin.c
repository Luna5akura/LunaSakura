// builtin.c

#include "stdio.h"
#include "string_util.h"
#include "mem.h"

#include "value.h"
#include "object.h"
#include "vm.h"

#include "builtin.h"

void init_dynamic_string(DynamicString* ds) {
  ds->length = 0;
  ds->capacity = 8;
  ds->chars = (char*)mmalloc(ds->capacity);
}

void append_char_to_dynamic_string(DynamicString* ds, char c) {
  if (ds->length + 1 >= ds->capacity) {
    ds->capacity *= 2;
    ds->chars = (char*)mmrealloc(ds->chars, ds->capacity);
  }
  ds->chars[ds->length++] = c;
}

void free_dynamic_string(DynamicString* ds) {
  mfree(ds->chars);
}

void list_add(ObjList* list, Value value) {
  write_value_array(&list->content, value);
}

Value native_print(VM* vm, int arg_count, Value* args) {
  for (int i = 0; i < arg_count; i++) {
    print_value(args[i]);
    if (i < arg_count - 1) {
      pprintf(" ");
    }
  }
  pprintf("\n");
  return NIL_VAL;
}

Value native_range(VM* vm, int arg_count, Value* args) {
  int start = 0;
  int end;
  int step = 1;

  if (arg_count == 1) {
    if (!IS_NUMBER(args[0])) {
      pprintf("range() requires integer arguments.\n");
      return NIL_VAL;
    }
    end = (int)AS_NUMBER(args[0]);
  } else if (arg_count == 2) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
      pprintf("rage() requires integer arguments.\n");
      return NIL_VAL;
    }
    start = (int)AS_NUMBER(args[0]);
    end = (int)AS_NUMBER(args[1]);
  } else if (arg_count == 3) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
      pprintf("range() requires integer arguments.\n");
      return NIL_VAL;
    }
    start = (int)AS_NUMBER(args[0]);
    end = (int)AS_NUMBER(args[1]);
    step = (int)AS_NUMBER(args[2]);
  } else {
    pprintf("range() takes 1 to 3 integer arguments (%d given).\n", arg_count);
    return NIL_VAL;
  }

  ObjRange* range = new_range(start, end, step);
  return OBJ_VAL(range);
}

Value native_input(VM* vm, int arg_count, Value* args) {
  if (arg_count > 1) {
    pprintf("input() takes 0 or 1 argument (%d given).\n", arg_count);
    return NIL_VAL;
  } else if (arg_count == 1) {
    Value prompt = args[0];
    if (AS_OBJ(prompt)->type != OBJ_STRING && AS_OBJ(prompt)->type != OBJ_NUMBER) {
      pprintf("input() only accept string and number as parameter.\n");
      return NIL_VAL;
    } else {
      print_value(prompt);
    }
  }

  int buffer_size = 1024;
  char* buffer = (char*)mmalloc(buffer_size);
  if (read(buffer, 1024) == -1) {
    return NIL_VAL;
  }

  int length = slen(buffer);
  if (length > 0 && buffer[length - 1] == '\n') {
    buffer[length - 1] = '\0';
    length--;
  }

  ObjString* input_string = copy_string(buffer, length);
  return OBJ_VAL(input_string);
}

Value iterator_next(ObjIterator* iterator) {
  Obj* iterable = iterator->iterable;
  switch (iterable->type) {
    case OBJ_LIST: {
      int idx = iterator->current_index++;
      ObjList* list = (ObjList*)iterable;
      if (idx == list->content.count) {
        return NIL_VAL;
      }
      return list->content.values[list->content.count - idx - 1];
    }
    case OBJ_STRING: {
      int idx = iterator->current_index++;
      ObjString* string = (ObjString*)iterable;
      if (idx == string->length) {
        return NIL_VAL;
      }
      char c = string->chars[idx];
      char chars[2] = {c, '\0'};
      ObjString* char_str = copy_string(chars, 1);

      return OBJ_VAL(char_str);
    }
    case OBJ_RANGE: {
      iterator->current_index++;
      ObjRange* range = (ObjRange*)iterable;
      int num = range->current;
      bool not_end = (range->step > 0) ? (range->current < range->end) : (range->current > range->end);
      if (not_end) {
        range->current += range->step;
        return NUMBER_VAL(num);
      }
      return NIL_VAL;
    }
    default:
      pprintf("Object is not iterable.\n");
      return NIL_VAL;
  }
}

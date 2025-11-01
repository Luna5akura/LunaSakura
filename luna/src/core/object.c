// object.c

#include "stdio.h"
#include "mem.h"

#include "object.h"

ObjString* copy_string(const char* chars, int length) {
    ObjString* string = (ObjString*)mmalloc(sizeof(ObjString));
    string->obj.type = OBJ_STRING;
    string->chars = (char*)mmalloc(length + 1);
    mcopy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->length = length;
    return string;
}

ObjNative* new_native(NativeFn function) {
    ObjNative* native = (ObjNative*)mmalloc(sizeof(ObjNative));
    native->obj.type = OBJ_NATIVE;
    native->function = function;
    return native;
}

ObjFunction* new_function() {
  ObjFunction* function = (ObjFunction*)mmalloc(sizeof(ObjFunction));
  function->obj.type = OBJ_FUNCTION;
  function->arity = 0;
  init_chunk(&function->chunk);
  function->name = nullptr;
  return function;
}

ObjList* new_list() {
  ObjList* list = (ObjList*)mmalloc(sizeof(ObjList));
  list->obj.type = OBJ_LIST;
  list->obj.list = list;
  init_value_array(&list->content);
  return list;
}

ObjIterator* new_iterator(Obj* iterable) {
  ObjIterator* iterator = (ObjIterator*)mmalloc(sizeof(ObjIterator));
  iterator->obj.type = OBJ_ITERATOR;
  iterator->iterable = iterable;
  iterator->current_index = 0;
  return iterator;
}

ObjRange* new_range(int start, int end, int step) {
  ObjRange* range = (ObjRange*)mmalloc(sizeof(ObjRange));
  range->obj.type = OBJ_RANGE;
  range->start = start;
  range->end = end;
  range->step = step;
  range->current = start;
  return range;
}

void print_object(Obj* obj) {
  if (!obj) return;
  switch (obj->type) {
    case OBJ_NUMBER:
      // TODO
      break;
    case OBJ_STRING:
      pprintf("%s", ((ObjString*)obj)->chars);
      break;
    case OBJ_BOOL:
      // TODO
      break;
    case OBJ_LIST:
      ObjList* list = obj->list;
      pprintf("[");
      for (size_t i = list->content.count; i > 0; i--) {
        print_value(list->content.values[i - 1]);
        if (i > 1) {
          pprintf(", ");
        }
      }
      pprintf("]");
      break;
    case OBJ_NONE:
      pprintf("None");
      break;
    case OBJ_FUNCTION:
      // TODO
      break;
    case OBJ_NATIVE:
      pprintf("<native function>");
      break;
    default:
      pprintf("<unknown object>");
      break;
  }
}


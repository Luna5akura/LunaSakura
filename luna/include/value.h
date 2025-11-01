// value.h

#ifndef VALUE_H
#define VALUE_H

typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjNative ObjNative;
typedef struct ObjFunction ObjFunction;
typedef struct ObjList ObjList;
typedef struct ObjIterator ObjIterator;
typedef struct ObjRange ObjRange;

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
} Value;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_STRING(value) (IS_OBJ(value) && AS_OBJ(value)->type == OBJ_STRING)
#define IS_LIST(value) (IS_OBJ(value) && AS_OBJ(value)->type == OBJ_LIST)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_NATIVE(value) (IS_OBJ(value) && AS_OBJ(value)->type == OBJ_NATIVE)
#define IS_FUNCTION(value) (IS_OBJ(value) && AS_OBJ(value)->type == OBJ_FUNCTION)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_LIST(value) (((ObjList*)AS_OBJ(value)))
#define AS_OBJ(value) ((value).as.obj)
#define AS_NATIVE(value)  (((ObjNative*)AS_OBJ(value))->function)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj*)object}})




typedef struct {
  int count;
  int capacity;
  Value* values;
} ValueArray;

void init_value_array(ValueArray* array);
void write_value_array(ValueArray* array, Value value);
void free_value_array(ValueArray* array);
void print_value(Value value);

#endif
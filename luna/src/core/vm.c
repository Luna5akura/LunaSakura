// vm.c

#include "stdio.h"
#include "string_util.h"
#include "mem.h"

#include "opcode.h"
#include "builtin.h"

#include "vm.h"


static bool is_falsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool values_equal(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL:
      return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
      return true;
    case VAL_NUMBER:
      return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
      if (AS_OBJ(a)->type != AS_OBJ(b)->type) return false;
      if (AS_OBJ(a)->type == OBJ_STRING) {
        return scmp(((ObjString*)AS_OBJ(a))->chars, ((ObjString*)AS_OBJ(b))->chars) == 0;
      }
      return AS_OBJ(a) == AS_OBJ(b);
    }

  }
}

void reset_stack(VM* vm) {
  vm->stack_top = vm->stack;
}

void init_vm(VM* vm) {
  reset_stack(vm);
  vm->environment = new_environment(nullptr);
  vm->frame_count = 0;
}

void free_vm(VM* vm) {
  if (vm->frame_count > 0) {
    ObjFunction* script_function = vm->frames[0].function;
    free_chunk(&script_function->chunk);
    mfree(script_function);
  }
  free_environment(vm->environment);
}

void push(VM* vm, Value value) {
  *vm->stack_top = value;
  vm->stack_top++;
}

Value pop(VM* vm) {
  vm->stack_top--;
  return *vm->stack_top;
}

static Value peek(VM* vm, int distance) {
  return vm->stack_top[-1 - distance];
}



void define_native(VM* vm, const char* name, NativeFn function) {
    ObjNative* native = new_native(function);
    Value value = OBJ_VAL(native);
    environment_set(vm->environment, name, value);
}

bool call(VM* vm, ObjFunction* function, int arg_count) {
  if (arg_count != function->arity) {
    pprintf("Expected %d arguments but got %d.", function->arity, arg_count);
    return false;
  }
  if (vm->frame_count == FRAMES_MAX) {
    pprintf("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm->frames[vm->frame_count++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm->stack_top - arg_count - 1;

  Environment* function_env = new_environment(vm->environment);
  vm->environment = function_env;

  for (int i = 0; i < arg_count; i ++) {
    const char* arg_name = function->arg_names[i];
    Value arg_value = vm->stack_top[-arg_count + i];
    environment_set(vm->environment, arg_name, arg_value);
  }

  vm->stack_top -=arg_count;

  return true;
}

bool call_value(VM* vm, Value callee, int arg_count) {
  if (IS_OBJ(callee)) {
    switch (AS_OBJ(callee)->type) {
      case OBJ_FUNCTION:
        return call(vm, AS_FUNCTION(callee), arg_count);
      case OBJ_NATIVE:
        // TODO
        NativeFn native = AS_NATIVE(callee);
        Value result = native(vm, arg_count, vm->stack_top - arg_count);
        vm->stack_top -= arg_count + 1;
        push(vm, result);
        return true;
      default:
        break;
    }
  }
  pprintf("Can only call functions and classes.\n");
  return false;
}

static InterpretResult run(VM* vm) {
  CallFrame* frame = &vm->frames[vm->frame_count - 1];

  #define READ_BYTE() (*(frame->ip++))
  #define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
  #define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
  #define READ_STRING() AS_STRING(READ_CONSTANT())
  #define BINARY_OP(valueType, op) \
  do { \
    if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
      pprintf("Operands must be numbers.\n"); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b = AS_NUMBER(pop(vm)); \
    double a = AS_NUMBER(pop(vm)); \
    push(vm, valueType(a op b)); \
  } while (false)

  for (;;) {
    // TODO debug
    uint8_t instruction;
    instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(vm, constant);
        break;
      }
      case OP_NIL: {
        push(vm, NIL_VAL);
        break;
      }
      case OP_TRUE:
        push(vm, BOOL_VAL(true));
        break;
      case OP_FALSE:
        push(vm, BOOL_VAL(false));
        break;
      case OP_POP: {
        pop(vm);
        break;
      }
      case OP_GET_VARIABLE: {
        ObjString* name = READ_STRING();
        Value value;
        if (!environment_get(vm->environment, name->chars, &value)) {
          pprintf("Undefined variable '%s'\n", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm, value);
        break;
      }
      case OP_DEFINE_VARIABLE: {
        ObjString* name = READ_STRING();
        // TODO memory free
        environment_set(vm->environment, name->chars, peek(vm, 0));
        pop(vm);
        break;
      }
      case OP_SET_VARIABLE: {
        ObjString* name = READ_STRING();
        environment_set(vm->environment, name->chars, peek(vm, 0));
        pop(vm);
        break;
      }
      case OP_ADD: {
        BINARY_OP(NUMBER_VAL, +);
        break;
      }
      case OP_SUBTRACT: {
        BINARY_OP(NUMBER_VAL, -);
        break;
      }
      case OP_MULTIPLY: {
        BINARY_OP(NUMBER_VAL, *);
        break;
      }
      case OP_DIVIDE: {
        BINARY_OP(NUMBER_VAL, /);
        break;
      }
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(vm, 0))) {
          pprintf("Operand must be a number.\n");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
        break;
      }
      case OP_EQUAL: {
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(values_equal(a, b)));
        break;
      }
      case OP_NOT_EQUAL: {
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(!values_equal(a, b)));
        break;
      }
      case OP_GREATER: {
        BINARY_OP(BOOL_VAL, >);
        break;
      }
      case OP_LESS: {
        BINARY_OP(BOOL_VAL, <);
        break;
      }
      case OP_GREATER_EQUAL: {
        BINARY_OP(BOOL_VAL, >=);
        break;
      }
      case OP_LESS_EQUAL: {
        BINARY_OP(BOOL_VAL, <=);
        break;
      }
      case OP_PRINT: {
        print_value(pop(vm));
        pprintf("\n");
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        Value condition = pop(vm);
        if (is_falsey(condition)) {
          frame->ip += offset;
        }
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_BUILD_LIST: {
        uint8_t item_count = READ_BYTE();
        ObjList* list = new_list();

        for (int i = item_count - 1; i >= 0 ; i--) {
          Value value = pop(vm);
          list_add(list, value);
        }
        push(vm, OBJ_VAL(list));
        break;
      }
      case OP_SUBSCRIPT: {
        Value index = pop(vm);
        Value sequence = pop(vm);

        if (IS_OBJ(sequence)) {
          switch (AS_OBJ(sequence)->type) {
            case OBJ_STRING: {
              ObjString* string = AS_STRING(sequence);

              if (!IS_NUMBER(index)) {
                pprintf("List indices must be numbers.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              int idx = (int)AS_NUMBER(index);
              if (idx < 0) idx += string->length;
              if (idx < 0 || idx >= string->length) {
                pprintf("String index out of range.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              char c = string->chars[idx];

              char chars[2] = {c, '\0'};
              ObjString* char_str = copy_string(chars, 1);
              push(vm, OBJ_VAL(char_str));
              break;
            }
            case OBJ_LIST: {
              ObjList* list = AS_LIST(sequence);

              if (!IS_NUMBER(index)) {
                pprintf("List indices must be numbers.\n");
                return INTERPRET_RUNTIME_ERROR;
              }
              int idx = (int)AS_NUMBER(index);
              if (idx < 0) idx += list->content.count;
              if (idx < 0 || idx >= list->content.count) {
                pprintf("List index out of range.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              push(vm, list->content.values[idx]);
              break;
            }
            default:
              pprintf("Object does not support indexing.\n");
              return INTERPRET_RUNTIME_ERROR;
          }
        } else {
          pprintf("Object is not subscriptable.\n");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SLICE: {
        Value step_val = pop(vm);
        Value end_val = pop(vm);
        Value start_val = pop(vm);
        Value sequence = pop(vm);

        if (IS_OBJ(sequence)) {
          switch (AS_OBJ(sequence)->type) {
            case OBJ_STRING: {
              ObjString* string = AS_STRING(sequence);

              int start, end, step;

              if (IS_NIL(step_val)) {
                step = 1;
              } else if (IS_NUMBER(step_val)) {
                step = (int)AS_NUMBER(step_val);
              } else {
                pprintf("Slice step must be a number.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              if (IS_NIL(start_val)) {
                if (step > 0) {
                  start = 0;
                } else {
                  start = -1;
                }
              } else if (IS_NUMBER(start_val)) {
                start = (int)AS_NUMBER(start_val);
              } else {
                pprintf("Slice start must be a number.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              if (IS_NIL(end_val)) {
                if (step > 0) {
                  end = -1;
                } else {
                  end = 0;
                }
              } else if (IS_NUMBER(end_val)) {
                end = (int)AS_NUMBER(end_val);
              } else {
                pprintf("Slice end must be a number.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              if (start < 0) start += string->length;
              if (end < 0) end += string->length;

              if (start < 0) start = 0;
              if (start > string->length) start = string->length;
              if (end < 0) end = 0;
              if (end > string->length) end = string->length;

              DynamicString builder;
              init_dynamic_string(&builder);

              if (step == 0) {
                pprintf("Slice step cannot be zero.\n");
                return INTERPRET_RUNTIME_ERROR;
              } else if (step > 0) {
                for (int i = start; i < end; i += step) {
                  append_char_to_dynamic_string(&builder, string->chars[i]);
                }
              } else {
                for (int i = start - 1; i >= end; i += step) {
                  append_char_to_dynamic_string(&builder, string->chars[i]);
                }
              }

              ObjString* result_string = copy_string(builder.chars, builder.length);
              free_dynamic_string(&builder);

              push(vm, OBJ_VAL(result_string));
              break;
            }

            case OBJ_LIST: {
              ObjList* list = AS_LIST(sequence);

              int start, end, step;

              if (IS_NIL(step_val)) {
                step = 1;
              } else if (IS_NUMBER(step_val)) {
                step = (int)AS_NUMBER(step_val);
              } else {
                pprintf("Slice step must be a number.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              if (IS_NIL(start_val)) {
                if (step > 0) {
                  start = 0;
                } else {
                  start = -1;
                }
              } else if (IS_NUMBER(start_val)) {
                start = (int)AS_NUMBER(start_val);
              } else {
                pprintf("Slice start must be a number.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              if (IS_NIL(end_val)) {
                if (step > 0) {
                  end = -1;
                } else {
                  end = 0;
                }
              } else if (IS_NUMBER(end_val)) {
                end = (int)AS_NUMBER(end_val);
              } else {
                pprintf("Slice end must be a number.\n");
                return INTERPRET_RUNTIME_ERROR;
              }

              if (start < 0) start += list->content.count;
              if (end < 0) end += list->content.count;

              if (start < 0) start = 0;
              if (start > list->content.count) start = list->content.count;
              if (end < 0) end = 0;
              if (end > list->content.count) end = list->content.count;

              ObjList* result_list = new_list();

              if (step == 0) {
                pprintf("Slice step cannot be zero.\n");
              } else if (step > 0) {
                for (int i = start; i < end; i += step) {
                  list_add(result_list, list->content.values[i]);
                }
              } else {
                for (int i = start - 1; i >= end; i += step) {
                  list_add(result_list, list->content.values[i]);
                }
              }

              push(vm, OBJ_VAL(result_list));
              break;
            }
            default:
              pprintf("Object does not support slicing.\n");
              return INTERPRET_RUNTIME_ERROR;
          }
        } else {
          pprintf("Object is not subscriptable.\n");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_ITERATOR: {
        Value iterable = peek(vm, 0);
        if (IS_OBJ(iterable)) {
          Obj* obj = AS_OBJ(iterable);
          if (obj->type != OBJ_LIST && obj->type != OBJ_STRING && obj->type != OBJ_RANGE) {
            pprintf("Object type %d is not iterable\n", obj->type);
            return INTERPRET_RUNTIME_ERROR;
          }
          Value iterator = OBJ_VAL(new_iterator(obj));
          vm->stack_top[-1] = iterator;
          break;
        } else {
          pprintf("Object is not iterable.\n");
          return INTERPRET_RUNTIME_ERROR;
        }
      }
      case OP_ITERATE: {
        Value iterator_val = peek(vm, 0);
        if (!IS_OBJ(iterator_val) || AS_OBJ(iterator_val)->type != OBJ_ITERATOR) {
          pprintf("Expected an iterator.\n");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjIterator* iterator = (ObjIterator*)AS_OBJ(iterator_val);

        ObjType type = iterator->iterable->type;

        if (type != OBJ_LIST && type != OBJ_STRING && type != OBJ_RANGE) {
          pprintf("Object is not iterable.\n");
          return INTERPRET_RUNTIME_ERROR;
        }

        Value result = iterator_next(iterator);
        if (result.type == VAL_NIL) {
          push(vm, BOOL_VAL(false));
        } else {
          push(vm, BOOL_VAL(true));
        }
        push(vm, result);
        break;
      }
      case OP_CALL: {
        int arg_count = READ_BYTE();
        Value callee = peek(vm, arg_count);
        if (!call_value(vm, callee, arg_count)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frame_count - 1];
        break;
      }
      case OP_RETURN: {
        Value result = pop(vm);
        pop(vm); // function itself
        vm->frame_count--;

        Environment* old_env = vm->environment;
        vm->environment = vm->environment->outer;
        free_environment(old_env);

        if (vm->frame_count == 0) {
          pop(vm);
          return INTERPRET_OK;
        }

        vm->stack_top = frame->slots;
        push(vm, result);

        frame = &vm->frames[vm->frame_count - 1];
        break;
      }
      default:
        pprintf("Unknown opcode %d\n", instruction);
      return INTERPRET_RUNTIME_ERROR;
    }
  }
  #undef READ_BYTE
  #undef READ_CONSTANT
  #undef READ_STRING
  #undef BINARY_OP
}

InterpretResult interpret(VM* vm, Chunk* chunk) {
  ObjFunction* script_function = new_function();
  script_function->arity = 0;
  script_function->name = NULL;

  script_function->chunk = *chunk;

  CallFrame* frame = &vm->frames[vm->frame_count++];
  frame->function = script_function;
  frame->ip = frame->function->chunk.code;
  frame->slots = vm->stack;

  define_native(vm, "print", native_print);
  define_native(vm, "range", native_range);
  define_native(vm, "input", native_input);

  InterpretResult result = run(vm);

  return result;
}



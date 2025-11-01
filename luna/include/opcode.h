// opcode.h

#ifndef OPCODE_H
#define OPCODE_H

typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_VARIABLE,
    OP_DEFINE_VARIABLE,
    OP_SET_VARIABLE,
    OP_PRINT,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_JUMP_IF_FALSE,
    OP_JUMP,
    OP_LOOP,
    OP_BUILD_LIST,
    OP_SUBSCRIPT,
    OP_SLICE,
    OP_GET_ITERATOR,
    OP_ITERATE,
    OP_CALL,
    OP_RETURN,
} OpCode;

#endif
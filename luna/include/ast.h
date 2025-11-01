//ast.h

#include "common.h"

#ifndef AST_H
#define AST_H

typedef enum {
  NODE_NUMBER,                 // 0
  NODE_STRING,                 // 1
  NODE_LIST,                   // 2
  NODE_IDENTIFIER,             // 3
  NODE_COMPARISON,             // 4
  NODE_BINARY_OP,              // 5
  NODE_UNARY_OP,               // 6
  NODE_EXPRESSION_STATEMENT,   // 7
  NODE_ASSIGNMENT,             // 8
  NODE_IF,                     // 9
  NODE_WHILE,                  // 10
  NODE_FOR,                    // 11
  NODE_BLOCK,                  // 12
  NODE_GETITEM,                // 13
  NODE_FUNCTION_DEFINITION,    // 14
  NODE_FUNCTION_CALL,          // 15
  NODE_RETURN,                 // 16
  NODE_PROGRAM,                // 17
} NodeType;

extern const char* token_type_names[];

typedef struct Node {
  NodeType type;
  size_t line;
  union {
    struct {
      double value;
    } number;

    struct {
      char* value;
    } string;

    struct {
      struct Node** content;
      size_t length;
    } list;

    struct {
      char* name;
    } identifier;

    struct {
      struct Node* left;
      struct Node* right;
      char* op;
    } comparison;

    struct {
      struct Node* left;
      struct Node* right;
      char* op;
    } binary_op;

    struct {
      struct Node* operand;
      char* op;
    } unary_op;

    struct {
      struct Node* expression;
    } expression_statement;

    struct {
      struct Node* left;
      struct Node* right;
    } assignment;

    struct {
      struct Node* condition;
      struct Node* then_branch;
      struct Node* else_branch;
    } if_statement;

    struct {
      struct Node* condition;
      struct Node* then_branch;
    } while_statement;

    struct {
      struct Node* element;
      struct Node* iterable;
      struct Node* then_branch;
    } for_statement;

    struct {
      struct Node** statements;
      size_t count;
    } block;

    struct {
      struct Node* sequence;
      struct Node* start;
      struct Node* end;
      struct Node* step;
    } getitem;

    struct {
      const char* function_name;
      struct Node** arguments;
      size_t arg_count;
      struct Node* content;
    } function_definition;

    struct {
      const char* function_name;
      struct Node** arguments;
      size_t arg_count;
    } function_call;

    struct {
      struct Node* value;
    } return_statement;

    struct {
      struct Node** statements;
      size_t count;
    } program;
  };
} Node;

Node* create_number_node(double value, size_t line);
Node* create_string_node(char* value, size_t line);
Node* create_list_node(Node** content, size_t length, size_t line);
Node* create_identifier_node(const char* name, size_t line);
Node* create_comparison_node(Node* left, Node* right, const char* op, size_t line);
Node* create_binary_op_node(Node* left, Node* right, const char* op, size_t line);
Node* create_unary_op_node(Node* operand, const char* op, size_t line);
Node* create_expression_statement_node(Node* expression, size_t line);
Node* create_assignment_node(Node* left, Node* right, size_t line);
Node* create_if_node(Node* condition, Node* then_branch, Node* else_branch, size_t line);
Node* create_while_node(Node* condition, Node* then_branch, size_t line);
Node* create_for_node(Node* element, Node* iterable, Node* then_branch, size_t line);
Node* create_block_node(Node** statements, size_t count, size_t line);
Node* create_getitem_node(Node* sequence, Node* start, Node* end, Node* step, size_t line);
Node* create_function_definition_node(const char* function_name, Node** arguments, size_t arg_count, Node* content, size_t line);
Node* create_function_call_node(const char* function_name, Node** arguments, size_t arg_count, size_t line);
Node* create_return_node(Node* value, size_t line);
Node* create_program_node(Node** statements, size_t count, size_t line);

void free_node(Node* node);
void print_node(Node* node);

#endif

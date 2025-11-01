// ast.c

#include "common.h"
#include "stdio.h"
#include "string_util.h"
#include "mem.h"

#include "ast.h"

const char* token_type_names[] = {
  "TOKEN_EOF",
  "TOKEN_NUMBER",
  "TOKEN_IDENTIFIER",
  "TOKEN_OPERATOR",
  "TOKEN_COLON",
  "TOKEN_PAREN_OPEN",
  "TOKEN_PAREN_CLOSE",
  "TOKEN_SQUARE_OPEN",
  "TOKEN_SQUARE_CLOSE",
  "TOKEN_BRACE_OPEN",
  "TOKEN_BRACE_CLOSE",
  "TOKEN_COMMA",
  "TOKEN_PUNCTUATION",
  "TOKEN_KEYWORD",
  "TOKEN_STRING",
  "TOKEN_CHAR",
  "TOKEN_IDNENT",
  "TOKEN_DEDENT",
  "TOKEN_NEWLINE",
  "TOKEN_UNKNOWN",
  "TOKEN_ERROR",
};

Node* create_number_node(double value, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_NUMBER;
  node->line = line;
  node->number.value = value;
  return node;
}

Node* create_string_node(char* value, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_STRING;
  node->line = line;
  node->string.value = value;
  return node;
}

Node* create_list_node(Node** content, size_t length, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_LIST;
  node->line = line;
  node->list.content = content;
  node->list.length = length;
  return node;
}

Node* create_identifier_node(const char* name, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_IDENTIFIER;
  node->line = line;
  node->identifier.name = sdup(name);
  return node;
}

Node* create_comparison_node(Node* left, Node* right, const char* op, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_COMPARISON;
  node->line = line;
  node->comparison.left = left;
  node->comparison.op = sdup(op);
  node->comparison.right = right;
  return node;
}

Node* create_binary_op_node(Node* left, Node* right, const char* op, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_BINARY_OP;
  node->line = line;
  node->binary_op.left = left;
  node->binary_op.op = sdup(op);
  node->binary_op.right = right;
  return node;
}

Node* create_unary_op_node(Node* operand, const char* op, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_UNARY_OP;
  node->line = line;
  node->unary_op.operand = operand;
  node->unary_op.op = sdup(op);
  return node;
}

Node* create_expression_statement_node(Node* expression, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_EXPRESSION_STATEMENT;
  node->line = line;
  node->expression_statement.expression = expression;
  return node;
}

Node* create_assignment_node(Node* left, Node* right, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_ASSIGNMENT;
  node->line = line;
  node->assignment.left = left;
  node->assignment.right = right;
  return node;
}

Node * create_if_node(Node* condition, Node* then_branch, Node* else_branch, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_IF;
  node->line = line;
  node->if_statement.condition = condition;
  node->if_statement.then_branch = then_branch;
  node->if_statement.else_branch = else_branch;
  return node;
}

Node* create_while_node(Node* condition, Node* then_branch, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_WHILE;
  node->line = line;
  node->while_statement.condition = condition;
  node->while_statement.then_branch = then_branch;
  return node;
}

Node* create_for_node(Node* element, Node* iterable, Node* then_branch, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_FOR;
  node->line = line;
  node->for_statement.element = element;
  node->for_statement.iterable = iterable;
  node->for_statement.then_branch = then_branch;
  return node;
}

Node* create_block_node(Node** statements, size_t count, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_BLOCK;
  node->line = line;
  node->block.statements = mmalloc(count * sizeof(Node*));
  for (size_t i = 0; i < count; i++) {
    node->block.statements[i] = statements[i];
  }
  node->block.count = count;
  return node;
}

Node* create_getitem_node(Node* sequence, Node* start, Node* end, Node* step, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_GETITEM;
  node->line = line;
  node->getitem.sequence = sequence;
  node->getitem.start = start;
  node->getitem.end = end;
  node->getitem.step = step;
  return node;
}

Node* create_function_definition_node(const char* function_name, Node** arguments, size_t arg_count, Node* content, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_FUNCTION_DEFINITION;
  node->line = line;
  node->function_definition.function_name = function_name;
  node->function_definition.arguments = arguments;
  node->function_definition.arg_count = arg_count;
  node->function_definition.content = content;

  return node;
}

Node* create_function_call_node(const char* function_name, Node** arguments, size_t arg_count, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_FUNCTION_CALL;
  node->line = line;
  node->function_call.function_name = function_name;
  node->function_call.arguments = arguments;
  node->function_call.arg_count = arg_count;

  return node;
}

Node* create_return_node(Node* value, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_RETURN;
  node->line = line;
  node->return_statement.value = value;
  return node;
}

Node* create_program_node(Node** statements, size_t count, size_t line) {
  Node* node = mmalloc(sizeof(Node));
  node->type = NODE_PROGRAM;
  node->line = line;
  node->program.statements = mmalloc(count * sizeof(Node*));
  for (size_t i = 0; i < count; i++) {
    node->program.statements[i] = statements[i];
  }
  node->program.count = count;
  return node;
}

void free_node(Node* node) {
  if (!node) return;

  switch (node->type) {
    case NODE_NUMBER:
      break;
    case NODE_STRING:
      mfree(node->string.value);
      break;
    case NODE_LIST:
      for (size_t i = 0; i < node->list.length; i++) {
        free_node(node->list.content[i]);
      }
      mfree(node->list.content);
      mfree(node);
      break;
    case NODE_IDENTIFIER:
      mfree(node->identifier.name);
      break;
    case NODE_COMPARISON:
      free_node(node->comparison.left);
      free_node(node->comparison.right);
      break;
    case NODE_BINARY_OP:
      free_node(node->binary_op.left);
      free_node(node->binary_op.right);
      break;
    case NODE_UNARY_OP:
      free_node(node->unary_op.operand);
      break;
    case NODE_EXPRESSION_STATEMENT:
      free_node(node->expression_statement.expression);
      break;
    case NODE_ASSIGNMENT:
      free_node(node->assignment.left);
      free_node(node->assignment.right);
      break;
    case NODE_IF:
      free_node(node->if_statement.condition);
      free_node(node->if_statement.then_branch);
      if (node->if_statement.else_branch) {
        free_node(node->if_statement.else_branch);
      }
      break;
    case NODE_WHILE:
      free_node(node->while_statement.condition);
      free_node(node->while_statement.then_branch);
      break;
    case NODE_FOR:
      free_node(node->for_statement.element);
      free_node(node->for_statement.iterable);
      free_node(node->for_statement.then_branch);
      break;
    case NODE_FUNCTION_DEFINITION:
      for (size_t i = 0; i < node->function_definition.arg_count; i++) {
        free_node(node->function_definition.arguments[i]);
      }
      mfree(node->function_definition.arguments);
      free_node(node->function_definition.content);
      mfree(node);
      break;
    case NODE_BLOCK:
      for (size_t i = 0; i < node->block.count; i++ ) {
        free_node(node->block.statements[i]);
      }
      mfree(node->block.statements);
      break;
    case NODE_GETITEM:
      free_node(node->getitem.sequence);
      free_node(node->getitem.start);
      free_node(node->getitem.end);
      free_node(node->getitem.step);
      break;
    case NODE_FUNCTION_CALL:
      // mfree(node->function_call.function_name);
      for (size_t i = 0; i < node->function_call.arg_count; i++ ) {
        free_node(node->function_call.arguments[i]);
      }
      mfree(node->function_call.arguments);
      mfree(node);
      break;
    case NODE_RETURN:
      free_node(node->return_statement.value);
      break;
    default: ;
  }
  mfree(node);
}

void print_node(Node* node) {
  if (!node) return;
  // pprintf("Printing Node type: %s", node->type);
  switch (node->type) {
    case NODE_NUMBER:
      pprintf("%f", node->number.value);
      break;
    case NODE_STRING:
      pprintf("'%s'", node->string.value);
      break;
    case NODE_LIST:
      pprintf("LIST<");
      for (size_t i = 0; i < node->list.length; i++) {
        print_node(node->list.content[i]);
        if (i < node->list.length - 1) {
          pprintf(", ");
        }
      }
    pprintf("> ");
    break;
    case NODE_IDENTIFIER:
      pprintf("%s", node->identifier.name);
      break;
    case NODE_COMPARISON:
      pprintf("(");
      print_node(node->comparison.left);
      pprintf("%s", node->comparison.op);
      print_node(node->comparison.right);
      pprintf(")");
      break;
    case NODE_BINARY_OP:
      pprintf("(");
      print_node(node->binary_op.left);
      pprintf(" %s ", node->binary_op.op);
      print_node(node->binary_op.right);
      pprintf(")");
      break;
    case NODE_UNARY_OP:
      pprintf("(");
      pprintf(" %s ", node->unary_op.op);
      print_node(node->unary_op.operand);
      pprintf(")");
      break;
    case NODE_EXPRESSION_STATEMENT:
      print_node(node->expression_statement.expression);
      break;
    case NODE_ASSIGNMENT:
      print_node(node->assignment.left);
      pprintf("<-");
      print_node(node->assignment.right);
      break;
    case NODE_IF:
      pprintf("IF {");
      print_node(node->if_statement.condition);
      pprintf("} ");
      pprintf("THEN {");
      print_node(node->if_statement.then_branch);
      pprintf("} ");
      if (node->if_statement.else_branch) {
        pprintf("ELSE {");
        print_node(node->if_statement.else_branch);
        pprintf("} ");
      }
      pprintf("ENDIF ");
      break;
    case NODE_WHILE:
      pprintf("WHILE {");
      print_node(node->while_statement.condition);
      pprintf("} ");
      pprintf("THEN {");
      print_node(node->while_statement.then_branch);
      pprintf("} ");
      pprintf("ENDWHILE ");
      break;
    case NODE_FUNCTION_DEFINITION:
      pprintf("DEF {<%s>(", node->function_definition.function_name);
      for (size_t i = 0; i < node->function_call.arg_count; i++) {
        print_node(node->function_call.arguments[i]);
        if (i < node->function_call.arg_count - 1) {
          pprintf(", ");
        }
      }
      pprintf(") {\n");
      print_node(node->function_definition.content);
      pprintf("}\n ENDDEF");
      break;
    case NODE_FOR:
      pprintf("FOR {");
      print_node(node->for_statement.element);
      pprintf(" IN ");
      print_node(node->for_statement.iterable);
      pprintf("} ");
      pprintf("THEN {");
      print_node(node->for_statement.then_branch);
      pprintf("} ");
      pprintf("ENDFOR ");
      break;
    case NODE_BLOCK:
      pprintf("(");
      for (size_t i = 0; i < node->block.count; i++ ) {
        pprintf("<");
        print_node(node->block.statements[i]);
        pprintf(">");
      }
      pprintf(")");
      break;
    case NODE_GETITEM:
      pprintf("GETITEM(<");
      print_node(node->getitem.sequence);
      pprintf("><");
      print_node(node->getitem.start);
      pprintf(":");
      print_node(node->getitem.end);
      pprintf(":");
      print_node(node->getitem.step);
      pprintf(">");
      break;
    case NODE_FUNCTION_CALL:
      pprintf("FUNCTION<%s>(", node->function_call.function_name);
      for (size_t i = 0; i < node->function_call.arg_count; i++) {
        print_node(node->function_call.arguments[i]);
        if (i < node->function_call.arg_count - 1) {
          pprintf(", ");
        }
      }
      pprintf(")");
      break;
    case NODE_PROGRAM:
      pprintf("(");
      for (size_t i = 0; i < node->program.count; i++ ) {
        pprintf("<");
        print_node(node->program.statements[i]);
        pprintf(">");
      }
      pprintf(")");
      break;
    case NODE_RETURN:
      pprintf("RETURN<");
      print_node(node->return_statement.value);
      pprintf(">");
      break;
    default:
      pprintf("Unknown token type: (%s)\n", token_type_names[node->type]);
  }
}

// parser.c

#include "stdio.h"
#include "ctype.h"
#include "string_util.h"
#include "stdlib.h"
#include "mem.h"
#include "ast.h"

#include "parser.h"

Parser* create_parser(LexerState* lexer) {
  Parser* parser = mmalloc(sizeof(Parser));
  parser->lexer = lexer;
  parser_advance(parser);
  return parser;
}

Node* parse_program(Parser* parser) {
  Node** statements = mmalloc(sizeof(Node*) * 100); // TODO
  size_t count = 0;

  while (parser->current_token.type != TOKEN_EOF) {
    statements[count++] = parse_statement(parser);
  }

  Node* program_node = create_program_node(statements, count, parser->lexer->line);
  mfree(statements);
  return program_node;
}

Node* parse_statement(Parser* parser) {
  while (parser->current_token.type == TOKEN_NEWLINE) {
    parser_advance(parser);
  }

  Node* node = nullptr;

  if (parser->current_token.type == TOKEN_KEYWORD) {
    if (scmp(parser->current_token.text, "if") == 0) {
      node = parse_if_statement(parser);
    } else if (scmp(parser->current_token.text, "while") == 0) {
      node = parse_while_statement(parser);
    } else if (scmp(parser->current_token.text, "for") == 0) {
      node = parse_for_statement(parser);
    } else if (scmp(parser->current_token.text, "def") == 0) {
      node = parse_def_statement(parser);
    } else if (scmp(parser->current_token.text, "return") == 0) {
      node = parse_return_statement(parser);
    }
  } else if (parser->current_token.type == TOKEN_IDENTIFIER) {

    if (scmp(lexer_peek_next_token(parser->lexer).text, "=") == 0) {
      node = parse_assignment(parser);
      return node;
    } else {
      Node* expression = parse_expression(parser);
      if (expression != NULL) {
        return create_expression_statement_node(expression, parser->lexer->line);
      }
      pprintf("Invalid statement starting with '%s' at position %d\n", parser->current_token.text, parser->lexer->position);
      parser_advance(parser); // Skip the token to prevent infinite loops
      return NULL;
    }
  }

  while (parser->current_token.type == TOKEN_NEWLINE) {
    parser_advance(parser);
  }

  return node;
}

Node* parse_assignment(Parser* parser) {
  Token identifier_token = parser->current_token;

  char *identifier = sdup(identifier_token.text);
  parser_advance(parser);
  if (parser->current_token.type == TOKEN_OPERATOR
    && scmp(parser->current_token.text, "=") ==0) {
    parser_advance(parser);
    Node* value = parse_expression(parser);
    return create_assignment_node(create_identifier_node(identifier, parser->lexer->line), value, parser->lexer->line);
  } else {
    parser->current_token = identifier_token;
    mfree(identifier);
    return nullptr;
  }
}

Node* parse_if_statement(Parser* parser) {
  parser_expect_keyword(parser, "if");
  parser_advance(parser);
  Node* condition = parse_expression(parser); // TODO: Change this

  parser_expect(parser, TOKEN_COLON);
  parser_advance(parser);

  parser_expect(parser, TOKEN_NEWLINE);
  parser_advance(parser);

  parser_expect(parser, TOKEN_INDENT);
  parser_advance(parser);

  Node* then_branch = parse_block(parser);
  Node* else_branch = NULL;

  parser_expect(parser, TOKEN_DEDENT);
  parser_advance(parser);

  while (parser->current_token.type == TOKEN_KEYWORD
    && (scmp(parser->current_token.text, "elif") == 0
      || scmp(parser->current_token.text, "else") == 0)) {
    if (scmp(parser->current_token.text, "elif") == 0) {
      parser_advance(parser);
      Node* elif_condition = parse_expression(parser);

      parser_expect(parser, TOKEN_COLON);
      parser_advance(parser);

      parser_expect(parser, TOKEN_NEWLINE);
      parser_advance(parser);

      parser_expect(parser, TOKEN_INDENT);
      parser_advance(parser);

      Node* elif_branch = parse_block(parser);

      then_branch = create_if_node(elif_condition, elif_branch, then_branch, parser->lexer->line);

      parser_expect(parser, TOKEN_DEDENT);
      parser_advance(parser);
    } else if (scmp(parser->current_token.text, "else") == 0) {
      parser_advance(parser);

      parser_expect(parser, TOKEN_COLON);
      parser_advance(parser);

      parser_expect(parser, TOKEN_NEWLINE);
      parser_advance(parser);

      parser_expect(parser, TOKEN_INDENT);
      parser_advance(parser);

      else_branch = parse_block(parser);

      parser_expect(parser, TOKEN_DEDENT);
      parser_advance(parser);
    }
  }

  return create_if_node(condition, then_branch, else_branch, parser->lexer->line);
}

Node* parse_while_statement(Parser* parser) {
  parser_expect_keyword(parser, "while");
  parser_advance(parser);
  Node* condition = parse_expression(parser);

  parser_expect(parser, TOKEN_COLON);
  parser_advance(parser);

  parser_expect(parser, TOKEN_NEWLINE);
  parser_advance(parser);

  parser_expect(parser, TOKEN_INDENT);
  parser_advance(parser);

  Node* then_branch = parse_block(parser);

  parser_expect(parser, TOKEN_DEDENT);
  parser_advance(parser);

  return create_while_node(condition, then_branch, parser->lexer->line);
}

Node* parse_for_statement(Parser* parser) {
  parser_expect_keyword(parser, "for");
  parser_advance(parser);

  parser_expect(parser, TOKEN_IDENTIFIER);
  Node* element = create_identifier_node(parser->current_token.text, parser->lexer->line);
  parser_advance(parser);

  parser_expect_keyword(parser, "in");
  parser_advance(parser);

  Node* iterable = parse_factor(parser);

  parser_expect(parser, TOKEN_COLON);
  parser_advance(parser);

  parser_expect(parser, TOKEN_NEWLINE);
  parser_advance(parser);

  parser_expect(parser, TOKEN_INDENT);
  parser_advance(parser);

  Node* then_branch = parse_block(parser);

  parser_expect(parser, TOKEN_DEDENT);
  parser_advance(parser);

  return create_for_node(element, iterable, then_branch, parser->lexer->line);
}

Node* parse_def_statement(Parser* parser) {
  Node** arguments = nullptr;
  size_t arg_count = 0;

  parser_expect_keyword(parser, "def");
  parser_advance(parser);

  parser_expect(parser, TOKEN_IDENTIFIER);
  char* identifier = sdup(parser->current_token.text);
  parser_advance(parser);

  parser_expect(parser, TOKEN_PAREN_OPEN);
  parser_advance(parser);

  if (parser->current_token.type != TOKEN_PAREN_CLOSE) {
    arguments = parse_arguments(parser, &arg_count);
  }

  parser_expect(parser, TOKEN_PAREN_CLOSE);
  parser_advance(parser);

  parser_expect(parser, TOKEN_COLON);
  parser_advance(parser);

  parser_expect(parser, TOKEN_NEWLINE);
  parser_advance(parser);

  parser_expect(parser, TOKEN_INDENT);
  parser_advance(parser);

  Node* content = parse_block(parser);

  if (content->block.statements[content->block.count - 1]->type != NODE_RETURN) {
    Node** statements = mmalloc(sizeof(Node*) * 2);
    statements[0] = content;
    statements[1]= create_return_node(nullptr, 0);
    content = create_block_node(statements, 2, parser->lexer->line);
  }

  parser_expect(parser, TOKEN_DEDENT);
  parser_advance(parser);

  return create_function_definition_node(identifier, arguments, arg_count, content, parser->lexer->line);
}

Node* parse_return_statement(Parser* parser) {
  parser_expect_keyword(parser, "return");
  parser_advance(parser);

  if (parser->current_token.type == TOKEN_NEWLINE) {
    parser_advance(parser);
    return create_return_node(nullptr, parser->lexer->line); // TODO change return value
  }

  Node* return_value = parse_expression(parser);

  parser_expect(parser, TOKEN_NEWLINE);
  parser_advance(parser);

  return create_return_node(return_value, parser->lexer->line);
}

Node* parse_expression(Parser* parser) {
  Node* left = parse_add_expr(parser);

  while (parser->current_token.type == TOKEN_OPERATOR
    && is_comparison_operator(parser->current_token.text)) {
    char* op = sdup(parser->current_token.text);
    parser_advance(parser);

    Node* right = parse_add_expr(parser);
    left = create_binary_op_node(left, right, op, parser->lexer->line);
    mfree(op);
  }

  return left;
}

Node* parse_add_expr(Parser* parser) {
  Node* left = parse_term(parser);

  while (parser->current_token.type == TOKEN_OPERATOR
    && is_add_minus(parser->current_token.text)) {

    char* op = sdup(parser->current_token.text);
    parser_advance(parser);

    Node* right = parse_term(parser);
    left = create_binary_op_node(left, right, op, parser->lexer->line);
    mfree(op);
  }

  return left;
}

Node* parse_term(Parser* parser) {
  Node* left = parse_factor(parser);

  while (parser->current_token.type == TOKEN_OPERATOR
    && is_mul_div(parser->current_token.text)) {

    char* op = sdup(parser->current_token.text);
    parser_advance(parser);

    Node* right = parse_factor(parser);
    left = create_binary_op_node(left, right, op, parser->lexer->line);
    mfree(op);
  }

  return left;
}

Node* parse_factor(Parser* parser) {
  if (parser->current_token.type == TOKEN_NUMBER) {
    double value = atof(parser->current_token.text);
    parser_advance(parser);
    return create_number_node(value, parser->lexer->line);
  } else if (parser->current_token.type == TOKEN_STRING) {
    char* value = sdup(parser->current_token.text);
    parser_advance(parser);
    return create_string_node(value, parser->lexer->line);
  } else if (parser->current_token.type == TOKEN_IDENTIFIER) {
    char* identifier = sdup(parser->current_token.text);
    parser_advance(parser);

    if (parser->current_token.type == TOKEN_PAREN_OPEN) {
      parser_advance(parser);
      Node** arguments = nullptr;
      size_t arg_count = 0;

      if (parser->current_token.type != TOKEN_PAREN_CLOSE) {
        arguments = parse_arguments(parser, &arg_count);
      }

      parser_expect(parser, TOKEN_PAREN_CLOSE);
      parser_advance(parser);

      return  create_function_call_node(identifier, arguments, arg_count, parser->lexer->line);
    } else if (parser->current_token.type == TOKEN_SQUARE_OPEN) {
      parser_advance(parser);

      if (parser->current_token.type == TOKEN_SQUARE_CLOSE) {
        pprintf("Expect index in line %d.", parser->lexer->line);
        return nullptr;
      }

      Node* sequence = create_identifier_node(identifier, parser->lexer->line);
      Node* start = nullptr;
      Node* end = nullptr;
      Node* step = nullptr;

      if (parser->current_token.type != TOKEN_COLON) {
        start = parse_factor(parser);
      }

      if (parser->current_token.type == TOKEN_SQUARE_CLOSE) {
        parser_advance(parser);
        return create_getitem_node(sequence, start, end, step, parser->lexer->line);
      }

      parser_expect(parser, TOKEN_COLON);
      parser_advance(parser);

      step = create_number_node(1, 0);

      if (parser->current_token.type == TOKEN_SQUARE_CLOSE) {
        parser_advance(parser);
        return create_getitem_node(sequence, start, end, step, parser->lexer->line);
      }

      if (parser->current_token.type != TOKEN_COLON) {
        end = parse_factor(parser);
      }

      if (parser->current_token.type == TOKEN_SQUARE_CLOSE) {
        parser_advance(parser);
        return create_getitem_node(sequence, start, end, step, parser->lexer->line);
      }

      parser_expect(parser, TOKEN_COLON);
      parser_advance(parser);

      if (parser->current_token.type == TOKEN_SQUARE_CLOSE) {
        parser_advance(parser);
        return create_getitem_node(sequence, start, end, step, parser->lexer->line);
      }

      step = parse_factor(parser);
      parser_expect(parser, TOKEN_SQUARE_CLOSE);
      parser_advance(parser);

      return create_getitem_node(sequence, start, end, step, parser->lexer->line);
    }
    else {
      return create_identifier_node(identifier, parser->lexer->line);
    }
  } else if (parser->current_token.type == TOKEN_OPERATOR && scmp(parser->current_token.text, "-") == 0) {
    parser_advance(parser);
    Node* operand = parse_factor(parser);
    return create_unary_op_node(operand, "-", parser->lexer->line);
  } else if (parser->current_token.type == TOKEN_PAREN_OPEN) {
    parser_advance(parser);
    Node* node = parse_expression(parser);
    parser_expect(parser, TOKEN_PAREN_CLOSE);
    parser_advance(parser);
    return node;
  } else if (parser->current_token.type == TOKEN_SQUARE_OPEN) {
    parser_advance(parser);
    Node** content = nullptr;
    size_t length = 0;

    if (parser->current_token.type != TOKEN_SQUARE_CLOSE) {
      content = parse_arguments(parser, &length);
    }

    parser_expect(parser, TOKEN_SQUARE_CLOSE);
    parser_advance(parser);
    return create_list_node(content, length, parser->lexer->line);
  }
  else {
    pprintf("Unexpected token '%s' at position %d\n", parser->current_token.text, parser->lexer->position);
    return nullptr;
  }
}

Node* parse_block(Parser* parser) {
  Node** statements = mmalloc(sizeof(Node*) * 100);
  size_t count = 0;

  while (parser->current_token.type != TOKEN_DEDENT
  && parser->current_token.type != TOKEN_EOF) {
    if (parser->current_token.type == TOKEN_NEWLINE) {
      parser_advance(parser);
      continue;
    }
    statements[count++] = parse_statement(parser);
  }

  return create_block_node(statements, count, parser->lexer->line);
}

Node** parse_arguments(Parser* parser, size_t* arg_count) {
  Node** arguments = mmalloc(sizeof(Node*) * 10); // TODO
  size_t count = 0;

  while (parser->current_token.type != TOKEN_PAREN_CLOSE
    && parser->current_token.type != TOKEN_EOF) {
    Node* arg = parse_expression(parser);
    if (arg == NULL) {
      break;
    }
    arguments[count++] = arg;

    if (parser->current_token.type == TOKEN_COMMA) {
      parser_advance(parser);
    } else {
      break;
    }
  }

  *arg_count = count;
  return arguments;
}

void parser_advance(Parser* parser) {
  if (parser->current_token.text) {
    mfree(parser->current_token.text);
    parser->current_token.text = nullptr;
  }
  parser->current_token = lexer_next_token(parser->lexer);
}

void parser_expect(Parser* parser, TokenType type) {
  if (parser->current_token.type != type) {
    pprintf("Unexpected type (%s), expected (%s)\n", token_type_names[parser->current_token.type], token_type_names[type]);
  }
}

void parser_expect_keyword(Parser* parser, const char* keyword) {
  if (!(parser->current_token.type == TOKEN_KEYWORD && scmp(parser->current_token.text, keyword) == 0)) {
    pprintf("Expected keyword '%s', got '%s' at position %d \n", keyword, parser->current_token.text, parser->lexer->position);
  }
}

void free_parser(Parser* parser) {
  mfree(parser);
}

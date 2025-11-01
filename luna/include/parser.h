// parser.h

#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
  LexerState* lexer;
  Token current_token;
} Parser;

Parser* create_parser(LexerState* lexer);

Node* parse_program(Parser* parser);
Node* parse_statement(Parser* parser);
Node* parse_assignment(Parser* parser);
Node* parse_if_statement(Parser* parser);
Node* parse_while_statement(Parser* parser);
Node* parse_for_statement(Parser* parser);
Node* parse_def_statement(Parser* parser);
Node* parse_return_statement(Parser* parser);
Node* parse_expression(Parser* parser);
Node* parse_add_expr(Parser* parser);
Node* parse_term(Parser* parser);
Node* parse_factor(Parser* parser);
Node* parse_block(Parser* parser);
Node** parse_arguments(Parser* parser, size_t* arg_count);
void parser_advance(Parser* parser);
void parser_expect(Parser* parser, TokenType type);
void parser_expect_keyword(Parser* parser, const char* keyword);
void free_parser(Parser* parser);

#endif

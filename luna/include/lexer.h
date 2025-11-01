// lexer.h

#include "common.h"

#ifndef LEXER_H
#define LEXER_H

typedef enum {
  TOKEN_EOF,
  TOKEN_NUMBER,
  TOKEN_IDENTIFIER,
  TOKEN_OPERATOR,
  TOKEN_COLON,
  TOKEN_PAREN_OPEN,
  TOKEN_PAREN_CLOSE,
  TOKEN_SQUARE_OPEN,
  TOKEN_SQUARE_CLOSE,
  TOKEN_BRACE_OPEN,
  TOKEN_BRACE_CLOSE,
  TOKEN_COMMA,
  TOKEN_PUNCTUATION,
  TOKEN_KEYWORD,
  TOKEN_STRING,
  TOKEN_CHAR,
  TOKEN_INDENT,
  TOKEN_DEDENT,
  TOKEN_NEWLINE,
  TOKEN_UNKNOWN,
  TOKEN_ERROR,
} TokenType;

typedef struct {
  TokenType type;
  char* text;
  size_t length;
} Token;

typedef struct {
  const char* source;
  size_t position;
  size_t length;
  size_t line;
  size_t column;
  int* indent_levels;
  int indent_level_count;
} LexerState;

void lexer_init(LexerState* state, const char* source_code);
Token lexer_peek_next_token(LexerState* state);
Token lexer_next_token(LexerState* state);
LexerState* create_lexer_from_string(const char* source_code);

#endif

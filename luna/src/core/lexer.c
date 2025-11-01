// lexer.c

#include "common.h"
#include "mem.h"
#include "ctype.h"
#include "string_util.h"

#include "lexer.h"

#define MAX_INDENT_LEVELS 100
#define TAB_SIZE_FOUR 4

void lexer_init(LexerState* state, const char* source_code) {
  state->source = source_code;
  state->position = 0;
  state->length = slen(source_code);
  state->line = 1;
  state->column = 1;
  state->indent_levels = (int*)mmalloc(sizeof(int) * MAX_INDENT_LEVELS);
  state->indent_level_count = 0;
  state->indent_levels[state->indent_level_count++] = 0;
}

char lexer_peek(LexerState* state) {
  return (state->position < state->length) ? state->source[state->position] : '\0';
}

char lexer_peek_next(LexerState* state) {
  return (state->position < state->length - 1) ? state->source[state->position + 1] : '\0';
}

void lexer_advance(LexerState* state) {
  if (state->position < state->length) {
    char c = state->source[state->position];
    state->position++;

    state->column++;
  }
}

Token lexer_next_token(LexerState* state) {
  Token token;
  token.type = TOKEN_EOF;
  token.text = nullptr;
  token.length = 0;

  while (1) {
    char c = lexer_peek(state);

    if (c == '\n') {
      lexer_advance(state);

      state->line++;
      state->column = 1;

      token.type = TOKEN_NEWLINE;
      token.text = sdup("\\n");
      token.length = 2;

      return token;
    }


    if (state->column == 1) {
      int current_indent = 0;
      while (c == ' ' || c == '\t') {
        if (c == ' ') {
          current_indent += 1;
        } else if (c == '\t') {
          current_indent += TAB_SIZE_FOUR;
        }
        lexer_advance(state);
        c = lexer_peek(state);
      }

      int previous_indent = state->indent_levels[state->indent_level_count - 1];

      if (current_indent > previous_indent) {
        if (state->indent_level_count < MAX_INDENT_LEVELS) {
          state->indent_levels[state->indent_level_count++] = current_indent;
          token.type = TOKEN_INDENT;
          token.text = sdup("INDE");
          token.length = 4;
        } else {
          token.type = TOKEN_ERROR;
          token.text = "Too many indentations";
          token.length = 21;
        }
        return token;
      } else if (current_indent < previous_indent) {
        while (state->indent_level_count > 0 // TODO change to 'if'
          && current_indent < state->indent_levels[state->indent_level_count - 1]) {
          state->indent_level_count--;
          token.type = TOKEN_DEDENT;
          token.text = sdup("DEDE");
          token.length = 4;
          return token;
        }
        if (current_indent != state->indent_levels[state->indent_level_count - 1]) {
          token.type = TOKEN_ERROR;
          token.text = "Indentation error";
          token.length = 17;
        }
        return token;
      }
    }

    while (is_whitespace(c)) {
      lexer_advance(state);
      c = lexer_peek(state);
    }

    if (c == '\0') {
      token.type = TOKEN_EOF;
      return token;
    }

    if (is_alpha(c)) {
      size_t start_pos = state-> position;

      while (is_alnum(lexer_peek(state))) {
        lexer_advance(state);
      }

      size_t len = state->position - start_pos;

      token.type = TOKEN_IDENTIFIER;
      token.text = sndup(state->source + start_pos, len);
      token.length = len;

      if (scmp(token.text, "if") == 0 || scmp(token.text, "while") == 0 || scmp(token.text, "for") == 0
      || scmp(token.text, "in") == 0 || scmp(token.text, "else") == 0 || scmp(token.text, "def") == 0
      || scmp(token.text, "return") == 0
      || scmp(token.text, "elif") == 0 || scmp(token.text, "match") == 0 || scmp(token.text, "case") == 0){
        token.type = TOKEN_KEYWORD;
      }

      return token;
    }

    if (is_digit(c)
        || (c == '-' && is_digit(lexer_peek_next(state)))) {
      size_t start_pos = state->position;
      int is_negative = 0;
      int has_decimal_point = 0;

      if (c == '-') {
        lexer_advance(state);
        is_negative = 1;
        c = lexer_peek(state);
      }

      while (is_digit(c)) {
        lexer_advance(state);
        c = lexer_peek(state);
      }

      if (c == '.') {
        lexer_advance(state);
        has_decimal_point = 1;
        c = lexer_peek(state);

        if (!is_digit(c)) {
          lexer_advance(state);
          size_t len = state->position - start_pos;

          token.type = TOKEN_UNKNOWN;
          token.text = sndup(state->source + start_pos, len);
          token.length = 1;
          return token;
        }

        while (is_digit(c)) {
          lexer_advance(state);
          c = lexer_peek(state);
        }
      }


      size_t len = state->position - start_pos;
      char* text = (char*)mmalloc(len + 1);
      size_t i;
      for (i = 0; i < len; i++) {
        text[i] = state->source[start_pos + i];
      }
      text[len] = '\0';

      token.type = TOKEN_NUMBER;
      token.text = text;
      token.length = len;

      return token;
    }

    if (c == '=' && lexer_peek_next(state) == '=') {
      lexer_advance(state);
      lexer_advance(state);

      token.type = TOKEN_OPERATOR;
      token.text = sdup("==");
      token.length = 2;
      return token;
    }

    if (c == '<' && lexer_peek_next(state) == '=') {
      lexer_advance(state);
      lexer_advance(state);

      token.type = TOKEN_OPERATOR;
      token.text = sdup("<=");
      token.length = 2;
      return token;
    }

    if (c == '>' && lexer_peek_next(state) == '=') {
      lexer_advance(state);
      lexer_advance(state);

      token.type = TOKEN_OPERATOR;
      token.text = sdup(">=");
      token.length = 2;
      return token;
    }

    if (c == '!' && lexer_peek_next(state) == '=') {
      lexer_advance(state);
      lexer_advance(state);

      token.type = TOKEN_OPERATOR;
      token.text = sdup("!=");
      token.length = 2;
      return token;
    }

    if (c == ':') {
      lexer_advance(state);

      char* text = (char*)mmalloc(2);
      text[0] = c;
      text[1] = '\0';

      token.type = TOKEN_COLON;
      token.text = text;
      token.length = 1;

      return token;
    }

    if (c == '(') {
      lexer_advance(state);
      token.type = TOKEN_PAREN_OPEN;
      token.text = sdup("(");
      token.length = 1;

      return token;
    }

    if (c == ')') {
      lexer_advance(state);
      token.type = TOKEN_PAREN_CLOSE;
      token.text = sdup(")");
      token.length = 1;

      return token;
    }

    if (c == '[') {
      lexer_advance(state);
      token.type = TOKEN_SQUARE_OPEN;
      token.text = sdup("[");
      token.length = 1;

      return token;
    }

    if (c == ']') {
      lexer_advance(state);
      token.type = TOKEN_SQUARE_CLOSE;
      token.text = sdup("]");
      token.length = 1;

      return token;
    }

    if (c == '{') {
      lexer_advance(state);
      token.type = TOKEN_BRACE_OPEN;
      token.text = sdup("{");
      token.length = 1;

      return token;
    }

    if (c == '}') {
      lexer_advance(state);
      token.type = TOKEN_BRACE_CLOSE;
      token.text = sdup("}");
      token.length = 1;

      return token;
    }

    if (c == ',') {
      lexer_advance(state);
      token.type = TOKEN_COMMA;
      token.text = sdup(",");
      token.length = 1;

      return token;
    }

    if (c == '+' || c == '-' || c == '*' || c == '/' ||
        c == '=' || c == '<' || c == '>' || c == '!' ||
        c == '&' || c == '|' || c == '^' || c == '%' ||
        c == ';' ) {

      lexer_advance(state);

      char* text = (char*)mmalloc(2);
      text[0] = c;
      text[1] = '\0';

      token.type = TOKEN_OPERATOR;
      token.text = text;
      token.length = 1;

      return token;
    }

    if (c == '\"' | c == '\'') {
      char prev_c = c;
      lexer_advance(state);
      size_t start_pos = state-> position;

      c = lexer_peek(state);
      while (c != prev_c && c != '\0') {
        lexer_advance(state);
        c = lexer_peek(state);
      }

      size_t len = state->position - start_pos;
      char* text = (char*)mmalloc(len + 1);
      size_t i;
      for (i = 0; i < len; i++) {
        text[i] = state->source[start_pos + i];
      }
      text[len] = '\0';

      if (c == prev_c) {
        lexer_advance(state);
      }

      token.type = TOKEN_STRING;
      token.text = text;
      token.length = len;

      return token;
    }

    lexer_advance(state);

    token.type = TOKEN_UNKNOWN;
    token.text = (char*)mmalloc(2);
    token.text[0] = c;
    token.text[1] = '\0';
    token.length = 1;

    return token;
  }
}

Token lexer_peek_next_token(LexerState* state) {
  size_t original_position = state->position;
  size_t original_line = state->line;
  size_t original_column = state->column;
  int original_indent_level_count = state->indent_level_count;
  int* original_indent_levels = state->indent_levels;

  Token next_token = lexer_next_token(state);

  state->position = original_position;
  state->line = original_line;
  state->column = original_column;
  state->indent_level_count = original_indent_level_count;
  state->indent_levels = original_indent_levels;

  return next_token;
}

void handle_indentation(LexerState* lexer) {

}

LexerState* create_lexer_from_string(const char* source_code) {
    LexerState* lexer = mmalloc(sizeof(LexerState));
    lexer_init(lexer, source_code);
    return lexer;
}

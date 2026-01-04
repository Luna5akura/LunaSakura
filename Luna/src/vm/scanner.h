// src/vm/scanner.h

#pragma once
#include "common.h"

typedef enum {
    // Single-character
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN, TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
    // One or two character
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // Literals (Range marked for O(1) checks)
    TOKEN_LITERAL_START,
    TOKEN_IDENTIFIER = TOKEN_LITERAL_START,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_LITERAL_END = TOKEN_NUMBER,
    // Keywords (Range marked)
    TOKEN_KEYWORD_START,
    TOKEN_AND = TOKEN_KEYWORD_START,
    TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,
    TOKEN_KEYWORD_END = TOKEN_WHILE,
    TOKEN_ERROR, TOKEN_EOF
} TokenType;

// --- Token ---
// 16 bytes exactly. Fits in 2x 64-bit registers.
typedef struct {
    const char* start;
    u32 line;
    u16 length;
    u8 type;
    u8 padding; // Explicit padding
} Token;

// --- Scanner Context ---
// 消除全局状态，支持多线程/多实例
typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

void initScanner(Scanner* scanner, const char* source);
Token scanToken(Scanner* scanner);
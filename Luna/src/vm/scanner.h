// src/vm/scanner.h

#pragma once
#include "common.h"
typedef enum {
    // 单字符 Token
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_COLON, TOKEN_SEMICOLON,
    TOKEN_SLASH, TOKEN_STAR,
    // 比较符号
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // 字面量
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // 关键字
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,
   
    // 循环控制
    TOKEN_CONTINUE, TOKEN_BREAK, TOKEN_IN,
    // Lambda关键字
    TOKEN_LAM,  // 新增: lam
    // 格式控制
    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_ERROR, TOKEN_EOF
} TokenType;
typedef struct {
    const char* start;
    u32 line;
    u16 length;
    u8 type;
    u8 padding;
} Token;
#define MAX_INDENT_STACK 256
typedef struct {
    const char* start;
    const char* current;
    u32 line;
   
    // 缩进控制状态
    u16 indentStack[MAX_INDENT_STACK]; // 使用 u16 节省空间 (缩进层级很少超过 65535 空格)
    i32 indentTop;
    i32 pendingDedents;
   
    bool isAtStartOfLine;
    i32 parenDepth; // 括号深度，用于忽略括号内的换行
} Scanner;
void initScanner(Scanner* scanner, const char* source);
Token scanToken(Scanner* scanner);
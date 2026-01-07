// src/vm/scanner.h

#pragma once
#include "common.h"
typedef enum {
    // 单字符 Token
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE, // [修改] 添加回 {}
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_COLON, // [新增] :
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
    // [新增] 循环控制
    TOKEN_CONTINUE, TOKEN_BREAK, TOKEN_IN,
    // [新增] Python 风格控制符
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
    i32 line;
 
    // [新增] 缩进控制状态
    int indentStack[MAX_INDENT_STACK];
    int indentTop;
    int pendingDedents;
    bool isAtStartOfLine;
    int parenDepth; // 用于处理括号内的换行（忽略）
} Scanner;
void initScanner(Scanner* scanner, const char* source);
Token scanToken(Scanner* scanner);
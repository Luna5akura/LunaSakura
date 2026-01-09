// src/core/scanner.h

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
    TOKEN_LAM, // 新增: lam
    // 格式控制
    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    // 异常处理
    TOKEN_TRY,
    TOKEN_EXCEPT,
    TOKEN_ERROR, TOKEN_EOF
} TokenType;

// --- Token Flags ---
#define TFLAG_NONE          0
#define TFLAG_IS_FLOAT      (1 << 0)  // 用于 TOKEN_NUMBER: 是否为浮点数
#define TFLAG_HAS_ESCAPES   (1 << 1)  // 用于 TOKEN_STRING: 是否包含转义序列
#define TFLAG_SUPPRESSED_NEWLINE (1 << 2)  // 用于 TOKEN_NEWLINE: 是否被括号抑制
#define TFLAG_SHORT_IDENT   (1 << 3)  // 用于 TOKEN_IDENTIFIER: 长度 < 4

typedef struct {
    const char* start; // 8 bytes
    u32 line; // 4 bytes
    u16 length; // 2 bytes
    u8 type; // 1 byte (TokenType)
    u8 flags;
} Token;

#define MAX_INDENT_STACK 64

typedef struct {
    // Hot data (accessed every char)
    const char* start; // 8
    const char* current; // 8
   
    // Warm data (accessed every token)
    u32 line; // 4
    i32 parenDepth; // 4
    // Cold data (accessed only at newlines/indents)
    i32 indentTop; // 4
    i32 pendingDedents; // 4
   
    // Array Data
    u16 indentStack[MAX_INDENT_STACK]; // 128 bytes
   
    bool isAtStartOfLine; // 1
    // Compiler adds 7 bytes padding at end to align struct size to 8
} Scanner;

static INLINE char peekChar(Scanner* scanner) {
    return *scanner->current;
}

void initScanner(Scanner* scanner, const char* source);
Token scanToken(Scanner* scanner);

// src/vm/scanner.h

#ifndef LUNA_SCANNER_H
#define LUNA_SCANNER_H

typedef enum {
    // 单字符符号
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN, // ( )
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE, // { } (虽然Python不用，但内部实现可能需要)
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,

    // 单或双字符符号
    TOKEN_BANG, TOKEN_BANG_EQUAL,       // ! !=
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,     // = ==
    TOKEN_GREATER, TOKEN_GREATER_EQUAL, // > >=
    TOKEN_LESS, TOKEN_LESS_EQUAL,       // < <=

    // 字面量
    TOKEN_IDENTIFIER, // 变量名 (如 Clip, my_video)
    TOKEN_STRING,     // "hello"
    TOKEN_NUMBER,     // 10.5

    // 关键字
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,

    TOKEN_ERROR, // 扫描出错
    TOKEN_EOF    // 文件结束
} TokenType;

typedef struct {
    TokenType type;
    const char* start; // 指向源码中该单词开始的位置
    int length;        // 单词长度
    int line;          // 行号 (用于报错)
} Token;

void initScanner(const char* source);
Token scanToken();

#endif
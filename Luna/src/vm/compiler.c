// src/vm/compiler.c

#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "scanner.h"
#include "vm.h"

// --- Parser Structure ---
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR, // or
    PREC_AND, // and
    PREC_EQUALITY, // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM, // + -
    PREC_FACTOR, // * /
    PREC_UNARY, // ! -
    PREC_CALL, // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

// === Global State ===
static Parser parser;
static Chunk* compilingChunk;
static Scanner scanner;
static VM* compilingVM;

static inline Chunk* currentChunk() {
    return compilingChunk;
}

// --- Error Handling ---
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
  
    fprintf(stderr, "[line %u] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// --- Lexical Stream ---
static void advance() {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken(&scanner);
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static inline bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// [新增] 专门用于匹配换行，允许在 EOF 处结束（文件最后一行可以没有换行符）
static void consumeLineEnd() {
    if (check(TOKEN_EOF)) return;
    consume(TOKEN_NEWLINE, "Expect newline after statement.");
}

// --- Bytecode Emission ---
static inline void emitByte(u8 byte) {
    writeChunk(compilingVM, currentChunk(), byte, (i32)parser.previous.line);
}

static inline void emitBytes(u8 byte1, u8 byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static inline void emitReturn() {
    emitByte(OP_RETURN);
}

static u8 makeConstant(Value value) {
    i32 constant = addConstant(compilingVM, currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (u8)constant;
}

static inline void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

// --- Forward Declarations ---
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// --- Parsing Rules ---
static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(compilingVM, parser.previous.start + 1,
                                    parser.previous.length - 2)));
}

static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitConstant(FALSE_VAL); break;
        case TOKEN_NIL: emitConstant(NIL_VAL); break;
        case TOKEN_TRUE: emitConstant(TRUE_VAL); break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));
    switch (operatorType) {
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        case TOKEN_LESS: emitByte(OP_LESS); break;
        default: return;
    }
}

static void call(bool canAssign) {
    u8 argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    emitBytes(OP_CALL, argCount);
}

static void variable(bool canAssign) {
    Token name = parser.previous;
    u8 arg = makeConstant(OBJ_VAL(copyString(compilingVM, name.start, name.length)));
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_GLOBAL, arg);
    } else {
        emitBytes(OP_GET_GLOBAL, arg);
    }
}

// --- Pratt Parser Rule Table ---
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE}, // 新增
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {NULL, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_GREATER] = {NULL, NULL, PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    
    // 新增 Token 规则占位
    [TOKEN_NEWLINE] = {NULL, NULL, PREC_NONE},
    [TOKEN_INDENT] = {NULL, NULL, PREC_NONE},
    [TOKEN_DEDENT] = {NULL, NULL, PREC_NONE},
    
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

// [新增] 块解析辅助函数 (供未来 if/while 使用)
// 语法: INDENT statement* DEDENT
static void block() {
    consume(TOKEN_INDENT, "Expect indentation at beginning of block.");
    
    while (!check(TOKEN_DEDENT) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_DEDENT, "Expect dedent at end of block.");
}

static void varDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect variable name.");
  
    u8 global = makeConstant(OBJ_VAL(copyString(compilingVM, parser.previous.start, parser.previous.length)));
    
    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitConstant(NIL_VAL);
    }
    // 使用换行符结束
    consumeLineEnd();
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void expressionStatement() {
    expression();
    // 使用换行符结束
    consumeLineEnd();
    emitByte(OP_POP);
}

static void printStatement() {
    expression();
    // 使用换行符结束
    consumeLineEnd();
    emitByte(OP_PRINT);
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } 
    // 未来在这里添加 if (match(TOKEN_IF)) ifStatement(); 等
    // ifStatement 示例:
    // consume(TOKEN_LEFT_PAREN); expression(); consume(TOKEN_RIGHT_PAREN);
    // consume(TOKEN_COLON); consume(TOKEN_NEWLINE);
    // block();
    else {
        expressionStatement();
    }
}

static void declaration() {
    // 处理多余的空行 (Scanner 可能会连续吐出 NEWLINE，如果没过滤干净)
    while (match(TOKEN_NEWLINE));

    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }
   
    if (parser.panicMode) {
        parser.panicMode = false;
        // 同步逻辑修改：寻找 NEWLINE 而不是分号
        while (parser.current.type != TOKEN_EOF) {
            if (parser.previous.type == TOKEN_NEWLINE) return;
            switch (parser.current.type) {
                case TOKEN_CLASS:
                case TOKEN_FUN:
                case TOKEN_VAR:
                case TOKEN_FOR:
                case TOKEN_IF:
                case TOKEN_WHILE:
                case TOKEN_PRINT:
                case TOKEN_RETURN:
                    return;
                default:
                    ;
            }
            advance();
        }
    }
}

bool compile(VM* vm, const char* source, Chunk* chunk) {
    initScanner(&scanner, source);
    compilingChunk = chunk;
    compilingVM = vm;
   
    parser.hadError = false;
    parser.panicMode = false;
   
    advance();
    
    // 跳过文件开头的空行
    while (match(TOKEN_NEWLINE));

    while (!match(TOKEN_EOF)) {
        declaration();
    }
    emitReturn();
   
    compilingVM = NULL;
    return !parser.hadError;
}
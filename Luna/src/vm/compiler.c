// src/vm/compiler.c

#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"

// 解析器状态
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode; // 用于错误恢复
} Parser;

// 优先级枚举（从低到高）
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

// 函数指针类型
typedef void (*ParseFn)(bool canAssign);

// 解析规则
typedef struct {
    ParseFn prefix;      // 前缀处理函数 (如 -1 的 -)
    ParseFn infix;       // 中缀处理函数 (如 1-2 的 -)
    Precedence precedence; // 优先级
} ParseRule;

Parser parser;
Chunk* compilingChunk; // 当前正在编译的字节码块

static Chunk* currentChunk() {
    return compilingChunk;
}

// === 错误处理 ===
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return; // 已经在恐慌模式就不重复报错了
    parser.panicMode = true;
    
    fprintf(stderr, "[line %d] Error", token->line);

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

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

// === 词法流控制 ===
static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
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

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// === 字节码生成辅助函数 ===
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

// === 前向声明 ===
static void expression();
static void parsePrecedence(Precedence precedence);
static ParseRule* getRule(TokenType type);

// === 解析逻辑 ===

// 处理数字: "123.4" -> OP_CONSTANT(123.4)
static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

// 处理括号: (1 + 2)
static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// 处理一元运算: -10
static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    // 编译操作数
    parsePrecedence(PREC_UNARY);

    // 发射操作指令
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return; // Unreachable.
    }
}

// 处理二元运算: 1 + 2
static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    
    // 递归解析右边的表达式，优先级要比当前算符高一级
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        default: return; // Unreachable.
    }
}


static void string(bool canAssign) {
    // parser.previous.start 指向左引号 "
    // +1 跳过左引号，长度 -2 去掉两个引号
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, 
                                    parser.previous.length - 2)));
}



// 1. 新增：变量解析辅助函数
static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);
    
    // 把变量名放入常量池，并返回其索引
    // 注意：这里我们只把名字存进去，还没存值
    return makeConstant(OBJ_VAL(copyString(parser.previous.start, parser.previous.length)));
}

static void defineVariable(uint8_t global) {
    emitBytes(OP_DEFINE_GLOBAL, global);
}

// 2. 新增：解析 var 声明
static void varDeclaration() {
    // 1. 解析变量名
    uint8_t global = parseVariable("Expect variable name.");

    // 2. 解析等号和初值
    if (match(TOKEN_EQUAL)) {
        expression(); // 编译等号右边的表达式，结果会留在栈顶
    } else {
        emitConstant(NIL_VAL); // 如果没写 =，默认为 nil
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    // 3. 生成定义指令
    defineVariable(global);
}

// 3. 新增：解析 print 语句
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

// 4. 新增：解析表达式语句 (比如 x = 1; 或 func();)
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP); // 表达式求值后，结果通常没用，弹出栈
}

// 5. 分流器：决定是 print 还是普通语句
static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else {
        expressionStatement();
    }
}

// 6. 分流器：决定是 var 声明 还是 普通语句
static void declaration() {
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    // 简单的错误恢复（如果出错，跳到下一个分号）
    if (parser.panicMode) {
        while (parser.current.type != TOKEN_EOF) {
            if (parser.previous.type == TOKEN_SEMICOLON) return;
            advance();
        }
    }
}

// 7. 关键：处理变量的使用 (Get/Set)
static void namedVariable(Token name, bool canAssign) {
    // 获取变量名在常量池的索引
    uint8_t arg = makeConstant(OBJ_VAL(copyString(name.start, name.length)));

    // 如果后面跟了等号，说明是赋值 set
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_GLOBAL, arg);
    } else {
        // 否则是读取 get
        emitBytes(OP_GET_GLOBAL, arg);
    }
}

// 注册到 rules 表里
static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

// 解析参数列表
static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression(); // 编译参数表达式
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}


static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}


// === Pratt 解析规则表 ===
// 这一步定义了所有 Token 在作为前缀或中缀时该怎么办
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL}, 
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},   // - 既可以是前缀(负号)也可以是中缀(减号)
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR}, // 乘除优先级更高
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_GREATER]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LESS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LESS_EQUAL]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE}, 
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},   // 数字只能出现在开头
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// 核心：Pratt 解析循环
static void parsePrecedence(Precedence precedence) {
    advance();
    // 1. 查找前缀解析函数 (比如遇到数字，或者负号)
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    // 2. 查找中缀解析函数 (比如遇到 + - * /)
    // 只有当新算符的优先级 > 当前优先级时，才继续解析
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}


bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;
    parser.hadError = false;
    parser.panicMode = false;

    advance();

    // 循环编译每一个声明，直到文件结束
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    emitReturn();
    return !parser.hadError;
}
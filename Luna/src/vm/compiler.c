// src/vm/compiler.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "vm.h"
#include "memory.h"
#include "object.h"

// ... (Struct definitions: Parser, ParseRule, Local, Loop, Compiler 保持不变)
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
} Local;

typedef struct {
    int enclosingLoopIndex;
    int depth;
    int start; 
    int bodyJump;
    int scopeDepth;
    int breakJumps[U8_COUNT];
    int breakCount;
    int continueJumps[U8_COUNT];
    int continueCount;
} Loop;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[U8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

static Parser parser;
static Scanner scanner;
static VM* compilingVM;
static Compiler* current = NULL;
static Loop* currentLoop = NULL;

// ... (Helper functions: errorAt, advance, consume, emitByte... 保持不变)
static Chunk* currentChunk() {
    return &current->function->chunk;
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF) fprintf(stderr, " at end");
    else if (token->type != TOKEN_ERROR) fprintf(stderr, " at '%.*s'", token->length, token->start);
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) { errorAt(&parser.previous, message); }
static void errorAtCurrent(const char* message) { errorAt(&parser.current, message); }

static void advance() {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken(&scanner);
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) { advance(); return; }
    errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }
static bool match(TokenType type) { if (!check(type)) return false; advance(); return true; }
static void consumeLineEnd() { if (!check(TOKEN_EOF)) consume(TOKEN_NEWLINE, "Expect newline."); }

static void emitByte(u8 byte) { writeChunk(compilingVM, currentChunk(), byte, parser.previous.line); }
static void emitBytes(u8 b1, u8 b2) { emitByte(b1); emitByte(b2); }

// [修改] 使用 OP_NIL
static void emitReturn() {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static u32 makeConstant(Value value) {  // 改为返回u32以支持更大索引
    i32 constant = addConstant(compilingVM, currentChunk(), value);
    if (constant > 16777215) {  // 3字节最大值 (2^24 - 1)
        error("Too many constants in one chunk.");
        return 0;
    }
    return (u32)constant;
}

static void emitConstant(Value value) {
    u32 constant = makeConstant(value);
    if (constant <= UINT8_MAX) {
        emitBytes(OP_CONSTANT, (u8)constant);
    } else {
        emitByte(OP_CONSTANT_LONG);
        emitByte((u8)(constant & 0xFF));
        emitByte((u8)((constant >> 8) & 0xFF));
        emitByte((u8)((constant >> 16) & 0xFF));
    }
}

static int emitJump(u8 instruction) {
    emitByte(instruction); emitByte(0xff); emitByte(0xff);
    return currentChunk()->count - 2;
}
static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) error("Too much code to jump over.");
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}
static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// Compiler Init
static void initCompiler(Compiler* compiler, VM* vm, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction(vm);
    current = compiler;
    
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(compilingVM, parser.previous.start, parser.previous.length);
    }
    
    // Reserve Slot 0
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
}

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) disassembleChunk(currentChunk(), function->name ? function->name->chars : "<script>");
#endif
    current = current->enclosing;
    return function;
}

static void beginScope() { current->scopeDepth++; }
static void endScope() {
    current->scopeDepth--;
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        emitByte(OP_POP);
        current->localCount--;
    }
}

// Rules
static void expression();
static void statement();
static void declaration();
static void block();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void addLocal(Token name) {
    if (current->localCount == U8_COUNT) { error("Too many locals."); return; }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
}

static void declareVariable() {
    if (current->scopeDepth == 0) return;
    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (name->length == local->name.length && memcmp(name->start, local->name.start, name->length) == 0) {
            error("Already a variable with this name in this scope.");
        }
    }
    addLocal(*name);
}

static void defineVariable(u8 global) {
    if (current->scopeDepth > 0) {
        current->locals[current->localCount - 1].depth = current->scopeDepth;
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (name->length == local->name.length && memcmp(name->start, local->name.start, name->length) == 0) {
            if (local->depth == -1) error("Read local in initializer.");
            return i;
        }
    }
    return -1;
}

static void namedVariable(Token name, bool canAssign) {
    u8 getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) { getOp = OP_GET_LOCAL; setOp = OP_SET_LOCAL; }
    else {
        arg = makeConstant(OBJ_VAL(copyString(compilingVM, name.start, name.length)));
        getOp = OP_GET_GLOBAL; setOp = OP_SET_GLOBAL;
    }
    if (canAssign && match(TOKEN_EQUAL)) { expression(); emitBytes(setOp, (u8)arg); }
    else emitBytes(getOp, (u8)arg);
}

// Implementations
static void number(bool canAssign) { emitConstant(NUMBER_VAL(strtod(parser.previous.start, NULL))); }
static void string(bool canAssign) { emitConstant(OBJ_VAL(copyString(compilingVM, parser.previous.start + 1, parser.previous.length - 2))); }

// [修改] 使用新 Opcode
static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL); break;
        case TOKEN_TRUE:  emitByte(OP_TRUE); break;
        default: return;
    }
}

static void grouping(bool canAssign) { expression(); consume(TOKEN_RIGHT_PAREN, "Expect ')'."); }
static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG:  emitByte(OP_NOT); break;
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
        case TOKEN_LESS_EQUAL: emitByte(OP_LESS_EQUAL); break;
        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_BANG_EQUAL: emitByte(OP_NOT_EQUAL); break;
        default: return;
    }
}
static void call(bool canAssign) {
    u8 argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do { expression(); if (argCount == 255) error("Max args."); argCount++; } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')'.");
    emitBytes(OP_CALL, argCount);
}
static void variable(bool canAssign) { namedVariable(parser.previous, canAssign); }

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT]           = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
    [TOKEN_COLON]         = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary, NULL, PREC_UNARY},
    [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
    [TOKEN_STRING]        = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
    [TOKEN_AND]           = {NULL, NULL, PREC_AND},
    [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
    [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
    [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
    [TOKEN_OR]            = {NULL, NULL, PREC_OR},
    [TOKEN_PRINT]         = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER]         = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS]          = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE]          = {literal, NULL, PREC_NONE},
    [TOKEN_VAR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }
static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) { error("Expect expression."); return; }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && match(TOKEN_EQUAL)) error("Invalid assignment target.");
}
static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

// --- Statements ---
static void block() {
    consume(TOKEN_INDENT, "Expect indentation.");
    beginScope();
    while (!check(TOKEN_DEDENT) && !check(TOKEN_EOF)) declaration();
    if (!check(TOKEN_EOF)) consume(TOKEN_DEDENT, "Expect dedent.");
    endScope();
}

static void funDeclaration() {
    u8 global = makeConstant(OBJ_VAL(copyString(compilingVM, parser.previous.start, parser.previous.length)));
    declareVariable();
    if (current->scopeDepth > 0) current->locals[current->localCount - 1].depth = current->scopeDepth;
    Compiler compiler;
    initCompiler(&compiler, compilingVM, TYPE_FUNCTION);
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '('.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) errorAtCurrent("Max args.");
            consume(TOKEN_IDENTIFIER, "Expect param.");
            declareVariable();
            defineVariable(0);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')'.");
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
    block();
    ObjFunction* function = endCompiler();
    emitBytes(OP_CONSTANT, makeConstant(OBJ_VAL(function)));
    if (current->scopeDepth == 0) emitBytes(OP_DEFINE_GLOBAL, global);
}

static void varDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect var name.");
    Token name = parser.previous;
    u32 global = 0;
    if (current->scopeDepth == 0) {
        ObjString* str = copyString(compilingVM, name.start, name.length);
        push(compilingVM, OBJ_VAL(str));  // GC保护
        global = makeConstant(OBJ_VAL(str));
    }
    declareVariable();
    if (match(TOKEN_EQUAL)) expression(); else emitConstant(NIL_VAL);
    consumeLineEnd();
    defineVariable((u8)global);
    if (current->scopeDepth == 0) pop(compilingVM);  // 弹出保护
}

// ... (beginLoop, patchLoopJumps, endLoop 保持不变)
static void beginLoop(Loop* loop) {
    loop->enclosingLoopIndex = currentLoop ? currentLoop->enclosingLoopIndex + 1 : 0;
    loop->start = currentChunk()->count;
    loop->scopeDepth = current->scopeDepth; // Use current compiler scope depth
    loop->breakCount = 0;
    loop->continueCount = 0;
    loop->depth = 0;
    currentLoop = loop;
}

static void patchLoopJumps(Loop* loop, int type) {
    int* jumps = (type == TOKEN_BREAK) ? loop->breakJumps : loop->continueJumps;
    int count = (type == TOKEN_BREAK) ? loop->breakCount : loop->continueCount;
    int target = (type == TOKEN_BREAK) ? currentChunk()->count : loop->start;
    for (int i = 0; i < count; i++) {
        int offset = jumps[i];
        int jump = (type == TOKEN_BREAK) ? target - offset - 2 : offset - target + 2;
        if (jump > UINT16_MAX) error("Loop jump too large.");
        currentChunk()->code[offset] = (jump >> 8) & 0xff;
        currentChunk()->code[offset + 1] = jump & 0xff;
    }
}
static void endLoop() {
    if (currentLoop->breakCount > U8_COUNT || currentLoop->continueCount > U8_COUNT) {
        error("Too many break/continue statements in loop.");
    }
    patchLoopJumps(currentLoop, TOKEN_BREAK);
    patchLoopJumps(currentLoop, TOKEN_CONTINUE);
    currentLoop = NULL;
}

static void ifStatement() {
    expression();
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    block();
    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);
    if (match(TOKEN_ELSE)) {
        consume(TOKEN_COLON, "Expect ':'.");
        consume(TOKEN_NEWLINE, "Expect newline.");
        block();
    }
    patchJump(elseJump);
}

static void whileStatement() {
    Loop loop;
    beginLoop(&loop);
    expression();
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
    loop.bodyJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    block();
    emitLoop(loop.start);
    patchJump(loop.bodyJump);
    emitByte(OP_POP);
    endLoop();
}

static void forStatement() {
    Loop loop;
    beginLoop(&loop);
    consume(TOKEN_IDENTIFIER, "Expect var name.");
    Token varName = parser.previous;
    consume(TOKEN_IN, "Expect 'in'.");
    expression();
    consume(TOKEN_DOT, "Expect '..'.");
    consume(TOKEN_DOT, "Expect '..'.");
    expression();
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");

    beginScope();
    addLocal(varName); defineVariable(0); // Var = Start (Stack N)
    addLocal((Token){.start="<end>",.length=5}); defineVariable(0); // End = End (Stack N+1)

    loop.start = currentChunk()->count;
    emitBytes(OP_GET_LOCAL, (u8)(current->localCount - 2)); 
    emitBytes(OP_GET_LOCAL, (u8)(current->localCount - 1));
    emitByte(OP_LESS_EQUAL);
    
    loop.bodyJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    
    consume(TOKEN_INDENT, "Expect indentation.");
    while (!check(TOKEN_DEDENT) && !check(TOKEN_EOF)) declaration();
    consume(TOKEN_DEDENT, "Expect dedent.");
    
    emitBytes(OP_GET_LOCAL, (u8)(current->localCount - 2));
    emitConstant(NUMBER_VAL(1));
    emitByte(OP_ADD);
    emitBytes(OP_SET_LOCAL, (u8)(current->localCount - 2));
    emitByte(OP_POP);
    emitLoop(loop.start);
    
    patchJump(loop.bodyJump);
    emitByte(OP_POP);
    endScope();
    endLoop();
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) error("Can't return from top-level.");
    if (match(TOKEN_NEWLINE)) emitReturn();
    else { expression(); consumeLineEnd(); emitByte(OP_RETURN); }
}

static void statement() {
    if (match(TOKEN_PRINT)) { expression(); consumeLineEnd(); emitByte(OP_PRINT); }
    else if (match(TOKEN_IF)) ifStatement();
    else if (match(TOKEN_RETURN)) returnStatement();
    else if (match(TOKEN_WHILE)) whileStatement();
    else if (match(TOKEN_FOR)) forStatement();
    else if (match(TOKEN_BREAK)) {
        if (!currentLoop) error("Break outside loop.");
        currentLoop->breakJumps[currentLoop->breakCount++] = emitJump(OP_JUMP);
        consumeLineEnd();
    } else if (match(TOKEN_CONTINUE)) {
        if (!currentLoop) error("Continue outside loop.");
        currentLoop->continueJumps[currentLoop->continueCount++] = emitJump(OP_LOOP);
        consumeLineEnd();
    } else { expression(); consumeLineEnd(); emitByte(OP_POP); }
}

static void declaration() {
    while (match(TOKEN_NEWLINE));
    if (check(TOKEN_DEDENT)) return;
    if (match(TOKEN_FUN)) { consume(TOKEN_IDENTIFIER, "Expect name."); funDeclaration(); }
    else if (match(TOKEN_VAR)) varDeclaration();
    else statement();
    if (parser.panicMode) {
        parser.panicMode = false;
        while (parser.current.type != TOKEN_EOF) {
            if (parser.previous.type == TOKEN_NEWLINE) return;
            advance();
        }
    }
}

bool compile(VM* vm, const char* source, Chunk* chunk) {
    initScanner(&scanner, source);
    compilingVM = vm;
    Compiler compiler;
    initCompiler(&compiler, vm, TYPE_SCRIPT);
    parser.hadError = false;
    parser.panicMode = false;
    advance();
    while (match(TOKEN_NEWLINE));
    while (!check(TOKEN_EOF)) declaration();
    while (match(TOKEN_NEWLINE));
    ObjFunction* function = endCompiler();
    if (!parser.hadError) {
        *chunk = function->chunk;
        function->chunk.code = NULL;
        function->chunk.constants.values = NULL;
        function->chunk.lineInfo.lines = NULL;
    }
    compilingVM = NULL;
    return !parser.hadError;
}
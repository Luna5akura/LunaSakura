// src/vm/compiler.c

#include <stdlib.h>
#include "compiler.h"
#include "scanner.h"
#include "memory.h"
// --- Structures ---
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_CONDITIONAL, // if ... else (新增)
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
typedef struct {
    Token name;
    i32 depth;
    bool isCaptured;
} Local;
typedef struct {
    u8 index;
    bool isLocal;
} Upvalue;
typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
    TYPE_INITIALIZER
} FunctionType;
// --- Compiler State ---
typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    Token name;
    bool hasSuperclass;
} ClassCompiler;
typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
  
    Local locals[U8_COUNT];
    i32 localCount;
    Upvalue upvalues[U8_COUNT];
    i32 scopeDepth;
} Compiler;
// [修复] 补充 continueJumps 和 continueCount
typedef struct Loop {
    struct Loop* enclosing; // 链表结构支持嵌套循环
    i32 start;
    i32 bodyJump; // 用于 while 的条件检查跳转
    i32 scopeDepth;
  
    // Break 补丁列表
    i32 breakJumps[U8_COUNT];
    i32 breakCount;
    // Continue 补丁列表
    i32 continueJumps[U8_COUNT];
    i32 continueCount;
} Loop;
// --- Global Context ---
static Parser parser;
static Scanner scanner;
static VM* compilingVM;
static Compiler* current = NULL;
static ClassCompiler* currentClass = NULL;
static Loop* currentLoop = NULL;
// --- Helpers ---
static Chunk* currentChunk() {
    return &current->function->chunk;
}
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
  
    fprintf(stderr, "[line %d] Error", token->line);
  
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type != TOKEN_ERROR) {
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
static bool check(TokenType type) {
    return parser.current.type == type;
}
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}
static void consumeLineEnd() {
    if (!check(TOKEN_EOF)) {
        consume(TOKEN_NEWLINE, "Expect newline.");
    }
}
// --- Bytecode Emitter ---
static void emitByte(u8 byte) {
    writeChunk(compilingVM, currentChunk(), byte, parser.previous.line);
}
static void emitBytes(u8 b1, u8 b2) {
    emitByte(b1);
    emitByte(b2);
}
static void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0); // 返回 this
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}
static u32 makeConstant(Value value) {
    i32 constant = addConstant(compilingVM, currentChunk(), value);
    if (constant > 16777215) {
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
static i32 emitJump(u8 instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}
static void patchJump(i32 offset) {
    i32 jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}
static void emitLoop(i32 loopStart) {
    emitByte(OP_LOOP);
    i32 offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
  
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}
// --- Compiler Management ---
static void initCompiler(Compiler* compiler, VM* vm, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
  
    compiler->function = newFunction(vm);
  
    current = compiler;
  
    if (type != TYPE_SCRIPT) {
        compiler->function->name = copyString(compilingVM, parser.previous.start, parser.previous.length);
    }
  
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
  
    if (type == TYPE_METHOD || type == TYPE_INITIALIZER) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}
static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
  
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        printf("---DISASSEMBLE CHUNK---\n");
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
    }
#endif
  
    current = current->enclosing;
    return function;
}
static void beginScope() {
    current->scopeDepth++;
}
static void endScope() {
    current->scopeDepth--;
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}
// --- Forward Declarations ---
static void expression();
static void statement();
static void declaration();
static void block();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
// --- Identifier & Variables ---
static Token syntheticToken(const char* text) {
    Token token;
    token.start = text;
    token.length = (i32)strlen(text);
    return token;
}
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}
static u8 identifierConstant(Token* name) {
    return (u8)makeConstant(OBJ_VAL(copyString(compilingVM, name->start, name->length)));
}
static void addLocal(Token name) {
    if (current->localCount == U8_COUNT) {
        error("Too many locals.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}
static i32 resolveLocal(Compiler* compiler, Token* name) {
    for (i32 i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}
static i32 addUpvalue(Compiler* compiler, u8 index, bool isLocal) {
    i32 upvalueCount = compiler->function->upvalueCount;
    for (i32 i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }
    if (upvalueCount == U8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }
    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}
static i32 resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;
    i32 local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (u8)local, true);
    }
    i32 upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (u8)upvalue, false);
    }
    return -1;
}
static void declareVariable() {
    if (current->scopeDepth == 0) return;
    Token* name = &parser.previous;
    for (i32 i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
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
static void namedVariable(Token name, bool canAssign) {
    u8 getOp, setOp;
    i32 arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (u8)arg);
    } else {
        emitBytes(getOp, (u8)arg);
    }
}
// --- Primitives ---
static void number(bool canAssign) {
    UNUSED(canAssign);
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}
static void string(bool canAssign) {
    UNUSED(canAssign);
    emitConstant(OBJ_VAL(copyString(compilingVM, parser.previous.start + 1, parser.previous.length - 2)));
}
static void literal(bool canAssign) {
    UNUSED(canAssign);
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return;
    }
}
static void grouping(bool canAssign) {
    UNUSED(canAssign);
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')'.");
}
static void unary(bool canAssign) {
    UNUSED(canAssign);
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG: emitByte(OP_NOT); break;
        default: return;
    }
}
static void binary(bool canAssign) {
    UNUSED(canAssign);
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));
    switch (operatorType) {
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_BANG_EQUAL: emitByte(OP_NOT_EQUAL); break;
        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS: emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitByte(OP_LESS_EQUAL); break;
        default: return;
    }
}
static void argumentList(u8* outArgCount, u8* outKwCount) {
    u8 argCount = 0;
    u8 kwCount = 0;
    bool isKeyword = false;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (check(TOKEN_RIGHT_PAREN)) break;

            // 预读检查 keyword: identifier = ...
            if (parser.current.type == TOKEN_IDENTIFIER && scanner.current[0] == '=') {
                isKeyword = true;
                
                // 压入参数名 (String Object)
                u8 nameConst = identifierConstant(&parser.current);
                emitBytes(OP_CONSTANT, nameConst); 
                consume(TOKEN_IDENTIFIER, "Expect keyword name.");
                consume(TOKEN_EQUAL, "Expect '='.");
                
                expression(); // 压入参数值
                
                if (kwCount == 255) error("Can't have more than 255 keyword arguments.");
                kwCount++;
            } else {
                if (isKeyword) {
                    error("Positional argument cannot follow keyword argument.");
                }
                expression();
                if (argCount == 255) error("Can't have more than 255 arguments.");
                argCount++;
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    *outArgCount = argCount;
    *outKwCount = kwCount;
}
static void call(bool canAssign) {
    UNUSED(canAssign);
    u8 argCount = 0;
    u8 kwCount = 0;
    argumentList(&argCount, &kwCount);

    if (kwCount > 0) {
        emitByte(OP_CALL_KW);
        emitByte(argCount);
        emitByte(kwCount);
    } else {
        emitBytes(OP_CALL, argCount);
    }
}
static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    u8 name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        // 赋值: obj.field = val
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        // 方法调用: obj.method(...)
        u8 argCount = 0;
        u8 kwCount = 0;
        
        // 解析参数列表
        argumentList(&argCount, &kwCount);

        if (kwCount > 0) {
            // 情况 A: 包含关键字参数
            // 无法使用 OP_INVOKE 优化，拆解为两步：
            // 1. 取出方法/字段 (OP_GET_PROPERTY)
            // 2. 执行关键字调用 (OP_CALL_KW)

            emitBytes(OP_INVOKE_KW, name); // 新指令
            emitByte(argCount);
            emitByte(kwCount);
        } else {
            // 情况 B: 仅有位置参数
            // 使用 OP_INVOKE 优化指令
            emitBytes(OP_INVOKE, name);
            emitByte(argCount);
        }
    } else {
        // 属性访问: obj.field
        emitBytes(OP_GET_PROPERTY, name);
    }
}
static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}
static void this_(bool canAssign) {
    UNUSED(canAssign);
    if (currentClass == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}
static void super_(bool canAssign) {
    UNUSED(canAssign);
    if (currentClass == NULL) error("Can't use 'super' outside of a class.");
    else if (!currentClass->hasSuperclass) error("Can't use 'super' in a class with no superclass.");
 
    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    u8 name = identifierConstant(&parser.previous);
 
    // 1. 压入 'this' (Receiver)
    namedVariable(syntheticToken("this"), false);
    
    if (match(TOKEN_LEFT_PAREN)) {
        u8 argCount = 0;
        u8 kwCount = 0;
        
        // 2. 解析参数 (压入 args 和 keywords)
        argumentList(&argCount, &kwCount);
        
        // 3. 压入 'super' (Superclass)
        namedVariable(syntheticToken("super"), false);
        
        if (kwCount > 0) {
            // [新增] 使用带关键字的 super 调用指令
            emitBytes(OP_SUPER_INVOKE_KW, name);
            emitByte(argCount);
            emitByte(kwCount);
        } else {
            emitBytes(OP_SUPER_INVOKE, name);
            emitByte(argCount);
        }
    } else {
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
    }
}
static void listLiteral(bool canAssign) {
    UNUSED(canAssign);
    u8 itemCount = 0;
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            if (check(TOKEN_RIGHT_BRACKET)) break;
            expression();
            if (itemCount == 255) error("Can't have more than 255 items in list.");
            itemCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after list.");
    emitBytes(OP_BUILD_LIST, itemCount);
}
static void dictLiteral(bool canAssign) {
    UNUSED(canAssign);
    u8 pairCount = 0;
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            if (check(TOKEN_RIGHT_BRACE)) break;
            expression();
            consume(TOKEN_COLON, "Expect ':' after dict key.");
            expression();
            if (pairCount == 255) error("Can't have more than 255 pairs in dict.");
            pairCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after dict.");
    emitBytes(OP_BUILD_DICT, pairCount);
}
// 新增: Lambda 表达式
static void lambda(bool canAssign) {
    UNUSED(canAssign);
    Compiler compiler;
    initCompiler(&compiler, compilingVM, TYPE_FUNCTION);
    beginScope();
    
    // Lambda 暂不支持默认参数，但需要修复 GC 安全性
    if (!check(TOKEN_COLON) && !check(TOKEN_LEFT_BRACE)) {
        do {
            if (current->function->arity >= 255) errorAtCurrent("Max args.");
            
            Token paramName = parser.current;
            ObjString* nameStr = copyString(compilingVM, paramName.start, paramName.length);
            push(compilingVM, OBJ_VAL(nameStr)); // [GC保护]

            consume(TOKEN_IDENTIFIER, "Expect param.");

            ObjFunction* f = current->function;
            f->paramNames = (ObjString**)reallocate(compilingVM, f->paramNames, 
                sizeof(ObjString*) * f->arity, sizeof(ObjString*) * (f->arity + 1));
            f->paramNames[f->arity] = nameStr;
            
            // [关键] 最后增加计数
            f->arity++;
            f->minArity++; // Lambda 默认必填

            declareVariable();
            defineVariable(0);
            
            pop(compilingVM); // [GC保护]
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_COLON, "Expect ':' after params.");
    
    if (match(TOKEN_LEFT_BRACE)) {
        block();
    } else {
        expression();
        emitByte(OP_RETURN);
    }
    
    ObjFunction* func = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(func)));
    for (i32 i = 0; i < func->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}
// 新增: 三元运算符 true_branch if cond else false_branch
static void conditional(bool canAssign) {
    UNUSED(canAssign);
    // 栈: true_branch (已解析的左侧表达式)
    // 解析 cond
    parsePrecedence((Precedence)(PREC_CONDITIONAL + 1));
    // 栈: true_branch, cond
    int falseJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // cond 为 true 时，弹出 cond，保留 true_branch
    int endJump = emitJump(OP_JUMP); // 从 true 分支跳过 false 分支
    patchJump(falseJump); // false 分支标签
    emitBytes(OP_POP, OP_POP); // cond 为 false 时，弹出 cond 和 true_branch
    consume(TOKEN_ELSE, "Expect 'else' after condition.");
    parsePrecedence(PREC_CONDITIONAL); // 解析 false_branch（支持右结合链式）
    patchJump(endJump); // 结束标签
}
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {listLiteral, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {dictLiteral, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, NULL, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, conditional, PREC_CONDITIONAL}, // 新增 infix for if
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LAM] = {lambda, NULL, PREC_PRIMARY}, // 新增: lam 作为前缀，优先级PRIMARY
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
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
static void block() {
    consume(TOKEN_INDENT, "Expect indentation.");
    beginScope();
    while (!check(TOKEN_DEDENT) && !check(TOKEN_EOF)) declaration();
    if (!check(TOKEN_EOF)) consume(TOKEN_DEDENT, "Expect dedent.");
    endScope();
}
static void parseFunctionParameters(FunctionType type) {
    bool isOptional = false; 
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (current->function->arity >= 255) errorAtCurrent("Max args.");

            // 1. 创建并保护参数名
            Token paramName = parser.current;
            ObjString* nameStr = copyString(compilingVM, paramName.start, paramName.length);
            push(compilingVM, OBJ_VAL(nameStr)); 

            consume(TOKEN_IDENTIFIER, "Expect param name.");
            
            declareVariable();
            defineVariable(0);

            // 2. 扩容数组
            ObjFunction* f = current->function;
            f->paramNames = (ObjString**)reallocate(compilingVM, f->paramNames, 
                sizeof(ObjString*) * f->arity, sizeof(ObjString*) * (f->arity + 1));
            
            // 3. 填入数据
            f->paramNames[f->arity] = nameStr;
            f->arity++;
            
            pop(compilingVM); // 弹出 nameStr

            // 4. 处理默认值
            if (match(TOKEN_EQUAL)) {
                isOptional = true;
                
                // [修复] 计算正确的 Slot 索引
                // 函数的局部变量从 slot 1 开始（slot 0 是函数自身）
                // 所以第 1 个参数 (arity=1) 对应 slot 1
                // 刚才 f->arity 已经加了 1，所以当前参数就是 f->arity 对应的 index
                // 或者直接用 localCount - 1 更稳妥
                u8 paramSlot = (u8)(current->localCount - 1);

                int jump = emitJump(OP_CHECK_DEFAULT);
                currentChunk()->count -= 3; 
                emitByte(OP_CHECK_DEFAULT);
                emitByte(paramSlot); 
                int checkJump = currentChunk()->count;
                emitByte(0xff);
                emitByte(0xff);

                expression();
                emitBytes(OP_SET_LOCAL, paramSlot);
                emitByte(OP_POP); 
                patchJump(checkJump);
            } else {
                if (isOptional) error("Non-default argument follows default argument.");
                f->minArity++;
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after params.");
}
static void function(FunctionType type); // Forward
// --- Loop Control ---
static void beginLoop(Loop* loop) {
    loop->enclosing = currentLoop;
    loop->start = currentChunk()->count;
    loop->scopeDepth = current->scopeDepth;
    loop->breakCount = 0;
    loop->continueCount = 0;
    currentLoop = loop;
}
static void endLoop() {
    // Patch Breaks
    for (i32 i = 0; i < currentLoop->breakCount; i++) {
        patchJump(currentLoop->breakJumps[i]);
    }
  
    // Patch Continues
    for (i32 i = 0; i < currentLoop->continueCount; i++) {
        i32 offset = currentLoop->continueJumps[i];
        i32 jumpDist = (offset + 2) - currentLoop->start;
        if (jumpDist > UINT16_MAX || jumpDist < 0) error("Loop jump too large.");
        currentChunk()->code[offset] = (jumpDist >> 8) & 0xff;
        currentChunk()->code[offset + 1] = jumpDist & 0xff;
    }
    currentLoop = currentLoop->enclosing;
}
static void whileStatement() {
    Loop loop;
    beginLoop(&loop);
    expression();
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
    i32 exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    block();
    emitLoop(loop.start);
    patchJump(exitJump);
    emitByte(OP_POP);
    endLoop();
}
static void forStatement() {
    consume(TOKEN_IDENTIFIER, "Expect var name.");
    Token varName = parser.previous;
    consume(TOKEN_IN, "Expect 'in'.");
    expression(); // Start
    consume(TOKEN_DOT, "Expect '..'.");
    consume(TOKEN_DOT, "Expect '..'.");
    expression(); // End
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
  
    beginScope();
    addLocal(varName); defineVariable(0);
    addLocal(syntheticToken("<limit>")); defineVariable(0);
  
    Loop loop;
    beginLoop(&loop);
  
    emitBytes(OP_GET_LOCAL, (u8)(current->localCount - 2));
    emitBytes(OP_GET_LOCAL, (u8)(current->localCount - 1));
    emitByte(OP_LESS_EQUAL);
  
    i32 exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    block();
  
    emitBytes(OP_GET_LOCAL, (u8)(current->localCount - 2));
    emitConstant(NUMBER_VAL(1));
    emitByte(OP_ADD);
    emitBytes(OP_SET_LOCAL, (u8)(current->localCount - 2));
    emitByte(OP_POP);
  
    emitLoop(loop.start);
    patchJump(exitJump);
    emitByte(OP_POP);
    endScope();
    endLoop();
}
static void breakStatement() {
    if (currentLoop == NULL) { error("Break outside loop."); return; }
    // [修复] 移除未使用的变量 localVarCount
    currentLoop->breakJumps[currentLoop->breakCount++] = emitJump(OP_JUMP);
    consumeLineEnd();
}
static void continueStatement() {
    if (currentLoop == NULL) { error("Continue outside loop."); return; }
    currentLoop->continueJumps[currentLoop->continueCount++] = emitJump(OP_LOOP);
    consumeLineEnd();
}
static void returnStatement() {
    if (current->type == TYPE_SCRIPT) error("Can't return from top-level.");
    if (match(TOKEN_NEWLINE)) emitReturn();
    else { expression(); consumeLineEnd(); emitByte(OP_RETURN); }
}
static void ifStatement() {
    expression();
    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
    i32 thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    block();
    i32 elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);
    if (match(TOKEN_ELSE)) {
        consume(TOKEN_COLON, "Expect ':'.");
        consume(TOKEN_NEWLINE, "Expect newline.");
        block();
    }
    patchJump(elseJump);
}
static void tryStatement() {
    consume(TOKEN_COLON, "Expect ':' after try.");
    consume(TOKEN_NEWLINE, "Expect newline after ':'.");
    int handlerPos = emitJump(OP_TRY);
    block();
    emitByte(OP_POP_HANDLER);
    int skipExcept = emitJump(OP_JUMP);
    patchJump(handlerPos);
    consume(TOKEN_EXCEPT, "Expect 'except' after try block.");
    consume(TOKEN_COLON, "Expect ':' after except.");
    consume(TOKEN_NEWLINE, "Expect newline after ':'.");
    block();
    patchJump(skipExcept);
}
static void statement() {
    if (match(TOKEN_PRINT)) { expression(); consumeLineEnd(); emitByte(OP_PRINT); }
    else if (match(TOKEN_IF)) ifStatement();
    else if (match(TOKEN_RETURN)) returnStatement();
    else if (match(TOKEN_WHILE)) whileStatement();
    else if (match(TOKEN_FOR)) forStatement();
    else if (match(TOKEN_BREAK)) breakStatement();
    else if (match(TOKEN_CONTINUE)) continueStatement();
    else if (match(TOKEN_TRY)) tryStatement();
    else { expression(); consumeLineEnd(); emitByte(OP_POP); }
}
static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, compilingVM, type);
    beginScope();

    // 替换原有的参数解析循环
    parseFunctionParameters(type);

    consume(TOKEN_COLON, "Expect ':'.");
    consume(TOKEN_NEWLINE, "Expect newline.");
    block();
    
    ObjFunction* func = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(func)));
    for (i32 i = 0; i < func->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}
static void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    u8 constant = identifierConstant(&parser.previous);
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    function(type);
    emitBytes(OP_METHOD, constant);
}
static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    u8 nameConstant = identifierConstant(&parser.previous);
    declareVariable();
    emitBytes(OP_CLASS, nameConstant);
    defineVariable(nameConstant);
    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    classCompiler.name = parser.previous;
    classCompiler.hasSuperclass = false;
    currentClass = &classCompiler;
    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);
        if (identifiersEqual(&className, &parser.previous)) error("A class can't inherit from itself.");
        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);
        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }
    namedVariable(className, false);
    consume(TOKEN_COLON, "Expect ':' after class declaration.");
    consume(TOKEN_NEWLINE, "Expect newline after ':'.");
    consume(TOKEN_INDENT, "Expect indentation for class body.");
    while (!check(TOKEN_DEDENT) && !check(TOKEN_EOF)) method();
    consume(TOKEN_DEDENT, "Expect dedent after class body.");
    emitByte(OP_POP);
    if (classCompiler.hasSuperclass) endScope();
    currentClass = currentClass->enclosing;
}
static void funDeclaration() {
    u8 global = identifierConstant(&parser.previous);
    declareVariable();
    if (current->scopeDepth > 0) current->locals[current->localCount - 1].depth = current->scopeDepth;
    function(TYPE_FUNCTION);
    defineVariable(global);
}
static void varDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect var name.");
    Token name = parser.previous;
    u8 global = identifierConstant(&name);
    if (match(TOKEN_EQUAL)) expression(); else emitByte(OP_NIL);
    consumeLineEnd();
    defineVariable(global);
}
static void declaration() {
    while (match(TOKEN_NEWLINE));
    if (check(TOKEN_DEDENT)) return;
    if (match(TOKEN_CLASS)) classDeclaration();
    else if (match(TOKEN_FUN)) { advance(); funDeclaration(); }
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
void markCompilerRoots(VM* vm) {
    Compiler* compiler = current;
    while (compiler != NULL) {
        if (compiler->function != NULL) markObject(vm, (Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
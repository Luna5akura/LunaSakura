// src/core/compiler/compiler_internal.h

#pragma once
#include "compiler.h"
#include "core/scanner.h"

// --- Structs & Enums ---

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_CONDITIONAL, // if ... else
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

typedef struct Loop {
    struct Loop* enclosing;
    i32 start;
    i32 bodyJump;
    i32 scopeDepth;
    i32 breakJumps[U8_COUNT];
    i32 breakCount;
    i32 continueJumps[U8_COUNT];
    i32 continueCount;
} Loop;

// --- Global State (Extern) ---
// 定义在 compiler.c 中
extern Parser parser;
extern Scanner scanner;
extern VM* compilingVM;
extern Compiler* current;
extern ClassCompiler* currentClass;
extern Loop* currentLoop;

// --- Shared Functions Prototypes ---

// Core / Helper (compiler.c)
Chunk* currentChunk();
void error(const char* message);
void errorAtCurrent(const char* message);
void errorAt(Token* token, const char* message);
void advance();
void consume(TokenType type, const char* message);
bool check(TokenType type);
bool match(TokenType type);
void consumeLineEnd();
void initCompiler(Compiler* compiler, VM* vm, FunctionType type);
ObjFunction* endCompiler();
void beginScope();
void endScope();

// Emitter (compiler_emit.c)
void emitByte(u8 byte);
void emitBytes(u8 b1, u8 b2);
void emitReturn();
u32 makeConstant(Value value);
void emitConstant(Value value);
i32 emitJump(u8 instruction);
void patchJump(i32 offset);
void emitLoop(i32 loopStart);

// Resolver (compiler_resolve.c)
Token syntheticToken(const char* text);
bool identifiersEqual(Token* a, Token* b);
u8 identifierConstant(Token* name);
void addLocal(Token name);
i32 resolveLocal(Compiler* compiler, Token* name);
i32 resolveUpvalue(Compiler* compiler, Token* name);
void declareVariable();
void defineVariable(u8 global);
void namedVariable(Token name, bool canAssign);

// Expressions (compiler_expr.c)
void expression();
void parsePrecedence(Precedence precedence);
ParseRule* getRule(TokenType type);
void argumentList(u8* outArgCount, u8* outKwCount); // Shared with super_

// Statements (compiler_stmt.c)
void declaration();
void statement();
void block();
void parseFunctionParameters(FunctionType type); // Shared with lambda
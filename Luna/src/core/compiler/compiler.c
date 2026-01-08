// src/core/compiler/compiler.c

#include <stdlib.h>
#include "compiler_internal.h"
#include "core/memory.h"
// --- Global Context Definitions ---
Parser parser;
Scanner scanner;
VM* compilingVM;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;
Loop* currentLoop = NULL;
// --- Helpers ---
Chunk* currentChunk() {
    return &current->function->chunk;
}
void errorAt(Token* token, const char* message) {
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
void error(const char* message) {
    errorAt(&parser.previous, message);
}
void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}
void advance() {
    parser.previous = parser.current;
 
    for (;;) {
        parser.current = scanToken(&scanner);
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}
void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}
bool check(TokenType type) {
    return parser.current.type == type;
}
bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}
void consumeLineEnd() {
    if (!check(TOKEN_EOF)) {
        consume(TOKEN_NEWLINE, "Expect newline.");
    }
}
// --- Management ---
void initCompiler(Compiler* compiler, VM* vm, FunctionType type) {
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
ObjFunction* endCompiler() {
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
void beginScope() {
    current->scopeDepth++;
}
void endScope() {
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
// --- Entry Points ---
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

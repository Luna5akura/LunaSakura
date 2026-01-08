// src/core/compiler/compiler_stmt.c

#include "compiler_internal.h"
#include "core/memory.h"

void block() {
    consume(TOKEN_INDENT, "Expect indentation.");
    beginScope();
    while (!check(TOKEN_DEDENT) && !check(TOKEN_EOF)) declaration();
    if (!check(TOKEN_EOF)) consume(TOKEN_DEDENT, "Expect dedent.");
    endScope();
}

void parseFunctionParameters(FunctionType type) {
    bool isOptional = false; 
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (current->function->arity >= 255) errorAtCurrent("Max args.");

            Token paramName = parser.current;
            ObjString* nameStr = copyString(compilingVM, paramName.start, paramName.length);
            push(compilingVM, OBJ_VAL(nameStr)); 

            consume(TOKEN_IDENTIFIER, "Expect param name.");
            
            declareVariable();
            defineVariable(0);

            ObjFunction* f = current->function;
            f->paramNames = (ObjString**)reallocate(compilingVM, f->paramNames, 
                sizeof(ObjString*) * f->arity, sizeof(ObjString*) * (f->arity + 1));
            
            f->paramNames[f->arity] = nameStr;
            f->arity++;
            
            pop(compilingVM);

            if (match(TOKEN_EQUAL)) {
                isOptional = true;
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

static void beginLoop(Loop* loop) {
    loop->enclosing = currentLoop;
    loop->start = currentChunk()->count;
    loop->scopeDepth = current->scopeDepth;
    loop->breakCount = 0;
    loop->continueCount = 0;
    currentLoop = loop;
}

static void endLoop() {
    for (i32 i = 0; i < currentLoop->breakCount; i++) {
        patchJump(currentLoop->breakJumps[i]);
    }
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

void statement() {
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
        namedVariable(parser.previous, false); 
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

void declaration() {
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
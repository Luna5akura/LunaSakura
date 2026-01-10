// src/core/compiler/compiler_expr.c

#include <stdlib.h>
#include "compiler_internal.h"
#include "core/memory.h"

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

void argumentList(u8* outArgCount, u8* outKwCount) {
    u8 argCount = 0;
    u8 kwCount = 0;
    bool isKeyword = false;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (check(TOKEN_RIGHT_PAREN)) break;

            if (parser.current.type == TOKEN_IDENTIFIER && peekChar(&scanner) == '=') {
                isKeyword = true;
                u8 nameConst = identifierConstant(&parser.current);
                emitBytes(OP_CONSTANT, nameConst); 
                consume(TOKEN_IDENTIFIER, "Expect keyword name.");
                consume(TOKEN_EQUAL, "Expect '='.");
                
                expression(); 
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
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        u8 argCount = 0;
        u8 kwCount = 0;
        argumentList(&argCount, &kwCount);

        if (kwCount > 0) {
            emitBytes(OP_INVOKE_KW, name); 
            emitByte(argCount);
            emitByte(kwCount);
        } else {
            emitBytes(OP_INVOKE, name);
            emitByte(argCount);
        }
    } else {
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
 
    namedVariable(syntheticToken("this"), false);
    
    if (match(TOKEN_LEFT_PAREN)) {
        u8 argCount = 0;
        u8 kwCount = 0;
        argumentList(&argCount, &kwCount);
        namedVariable(syntheticToken("super"), false);
        
        if (kwCount > 0) {
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

    // --- 1. 前瞻扫描 (Lookahead) ---
    bool isComprehension = false;
    Scanner initialScanner = scanner;
    Token initialCurrent = parser.current;
    
    int nesting = 0;
    Scanner probe = scanner; 
    
    if (parser.current.type != TOKEN_FOR) {
        while (true) {
            Token t = scanToken(&probe);
            if (t.type == TOKEN_LEFT_BRACKET) nesting++;
            else if (t.type == TOKEN_RIGHT_BRACKET) {
                if (nesting == 0) break; 
                nesting--;
            }
            else if (t.type == TOKEN_FOR && nesting == 0) {
                isComprehension = true;
                break;
            }
            else if (t.type == TOKEN_EOF) break;
        }
    }

    // --- 2. 分支处理 ---
    
    if (isComprehension) {
        // === 推导式编译逻辑 ===
        
        // 1. 创建结果列表
        emitBytes(OP_BUILD_LIST, 0);

        // [关键修正] 开启新的作用域，确保变量被视为局部变量，避免 OP_DEFINE_GLOBAL
        beginScope();

        // 将列表注册为临时局部变量
        addLocal(syntheticToken("(list)"));
        defineVariable(0);
        u8 listSlot = (u8)(current->localCount - 1);
        
        // 2. 跳转 Scanner 到 'for' 之后
        scanner = probe; 
        advance(); // Current becomes 'var'
        
        Token varName = parser.current;
        consume(TOKEN_IDENTIFIER, "Expect variable name after 'for'.");
        consume(TOKEN_IN, "Expect 'in' after variable name.");
        
        // 3. 编译 iterable
        expression(); 
        
        Scanner endScanner = scanner;
        Token endCurrent = parser.current;
        
        // 4. 注册迭代器相关变量
        addLocal(syntheticToken("(iterable)"));
        defineVariable(0);
        
        emitByte(OP_ITER_INIT);
        addLocal(syntheticToken("(iterator)"));
        defineVariable(0);
        
        // 循环开始
        Loop loop;
        loop.enclosing = currentLoop;
        loop.start = getChunkCount(currentChunk());
        loop.scopeDepth = current->scopeDepth;
        loop.localCount = current->localCount; 
        currentLoop = &loop;
        
        int exitJump = emitJump(OP_ITER_NEXT);
        
        // 进入循环体 Scope (var 所在)
        beginScope();
        addLocal(varName); 
        defineVariable(0);
       

        // 5. 回跳编译 expression
        scanner = initialScanner;
        parser.current = initialCurrent;
        
        expression(); 
        consume(TOKEN_FOR, "Expect 'for' in comprehension."); 
        
        // 6. 追加结果
        emitBytes(OP_LIST_APPEND, listSlot);
        
        // 7. 循环收尾
        endScope(); // 弹出 var (循环变量)
        
        emitLoop(loop.start);
        patchJump(exitJump);
        currentLoop = loop.enclosing;
        
        // === 修复开始 ===
        // 替换掉原有的手动 POP + endScope 逻辑
        
        // 1. 弹出 iterator (栈顶)
        emitByte(OP_POP);
        current->localCount--; // 从编译器记录中移除 (iterator)
        
        // 2. 弹出 iterable (次栈顶)
        emitByte(OP_POP);
        current->localCount--; // 从编译器记录中移除 (iterable)
        
        // 3. 处理结果列表 (list)
        // 我们不生成 OP_POP，因为我们需要这个列表留在运行时栈上作为表达式的结果。
        // 但是，我们必须从编译器的 locals 数组中移除它，否则后续的变量声明会计算出错误的 slot 索引。
        current->localCount--; // 从编译器记录中移除 (list)

        // 4. 手动结束作用域
        // 我们已经手动清理了所有局部变量，现在只需调整作用域深度
        current->scopeDepth--;
        // === 修复结束 ===

        // 8. 恢复 Scanner
        scanner = endScanner;
        parser.current = endCurrent;
        
        consume(TOKEN_RIGHT_BRACKET, "Expect ']' after comprehension.");
    } else {
        // === 普通列表编译逻辑 ===
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

static void lambda(bool canAssign) {
    UNUSED(canAssign);
    Compiler compiler;
    initCompiler(&compiler, compilingVM, TYPE_FUNCTION);
    beginScope();
    
    ObjString** tempParams = NULL;
    int paramCapacity = 0;
    int paramCount = 0;

    // 检查是否有参数列表
    if (!check(TOKEN_COLON) && !check(TOKEN_LEFT_BRACE)) {
         do {
            if (paramCount >= 255) {
                errorAtCurrent("Max args.");
            }
            
            Token paramName = parser.current;
            ObjString* nameStr = copyString(compilingVM, paramName.start, paramName.length);
            push(compilingVM, OBJ_VAL(nameStr)); // GC 保护
            consume(TOKEN_IDENTIFIER, "Expect param.");
            
            // [优化] 1. 临时数组容量检查与几何增长
            if (paramCount + 1 > paramCapacity) {
                int oldCapacity = paramCapacity;
                paramCapacity = GROW_CAPACITY(oldCapacity);
                // 扩容临时数组，而不是 ObjFunction 的属性
                tempParams = (ObjString**)reallocate(compilingVM, tempParams, 
                    sizeof(ObjString*) * oldCapacity, 
                    sizeof(ObjString*) * paramCapacity);
            }
            
            // [优化] 2. 存入临时数组
            tempParams[paramCount++] = nameStr;

            // 处理局部变量声明 (compiler.locals)
            declareVariable();
            defineVariable(0);
            
            pop(compilingVM); // nameStr 已经进入 locals 或 tempParams，可以解除栈顶保护
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_COLON, "Expect ':' after params.");
    
    // [优化] 3. 循环结束，一次性提交给 ObjFunction
    // 将 tempParams 收缩到实际大小 (Shrink to fit)，避免浪费内存
    ObjFunction* f = current->function;
    f->arity = paramCount;
    f->minArity = paramCount; // 假设 Lambda 没有默认参数，minArity = arity
    
    if (paramCount > 0) {
        // 将临时数组的所有权转移给 function，并收缩多余容量
        f->paramNames = (ObjString**)reallocate(compilingVM, tempParams, 
            sizeof(ObjString*) * paramCapacity, 
            sizeof(ObjString*) * paramCount);
    } else {
        // 如果没有参数，paramNames 应为 NULL
        // 如果 tempParams 分配过内存（极少见情况），需要释放
        if (tempParams != NULL) {
            reallocate(compilingVM, tempParams, sizeof(ObjString*) * paramCapacity, 0);
        }
        f->paramNames = NULL;
    }

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

static void conditional(bool canAssign) {
    UNUSED(canAssign);
    parsePrecedence((Precedence)(PREC_CONDITIONAL + 1));
    int falseJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); 
    int endJump = emitJump(OP_JUMP); 
    patchJump(falseJump); 
    emitBytes(OP_POP, OP_POP); 
    consume(TOKEN_ELSE, "Expect 'else' after condition.");
    parsePrecedence(PREC_CONDITIONAL); 
    patchJump(endJump); 
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
    [TOKEN_IF] = {NULL, conditional, PREC_CONDITIONAL},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LAM] = {lambda, NULL, PREC_PRIMARY},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

ParseRule* getRule(TokenType type) { return &rules[type]; }

void parsePrecedence(Precedence precedence) {
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

void expression() { parsePrecedence(PREC_ASSIGNMENT); }
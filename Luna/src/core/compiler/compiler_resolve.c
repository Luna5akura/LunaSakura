// src/core/compiler/compiler_resolve.c

#include "compiler_internal.h"

Token syntheticToken(const char* text) {
    Token token;
    token.start = text;
    token.length = (i32)strlen(text);
    return token;
}

bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

u32 identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(compilingVM, name->start, name->length)));
}

void addLocal(Token name) {
    if (current->localCount == U8_COUNT) {
        error("Too many locals.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

i32 resolveLocal(Compiler* compiler, Token* name) {
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

i32 resolveUpvalue(Compiler* compiler, Token* name) {
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

void declareVariable() {
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

void defineVariable(u32 global) {
    if (current->scopeDepth > 0) {
        current->locals[current->localCount - 1].depth = current->scopeDepth;
        return;
    }
    if (global > 255) {
        // 假设你定义了 OP_DEFINE_GLOBAL_LONG
        // 如果没定义，至少要报错，而不是截断
        // emitByte(OP_DEFINE_GLOBAL_LONG);
        // emitByte((global >> 0) & 0xff);
        // emitByte((global >> 8) & 0xff);
        // emitByte((global >> 16) & 0xff);
        error("Too many globals (limit 255). Implement OP_DEFINE_GLOBAL_LONG to fix.");
    } else {
        emitBytes(OP_DEFINE_GLOBAL, (u8)global);
    }
}

void namedVariable(Token name, bool canAssign) {
    u8 getOp, setOp;
    i32 arg = resolveLocal(current, &name);
    
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        // 局部变量 arg 是 u8 (localCount 上限是 256)，这里安全
        if (canAssign && match(TOKEN_EQUAL)) {
            expression();
            emitBytes(setOp, (u8)arg);
        } else {
            emitBytes(getOp, (u8)arg);
        }
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
        // Upvalue 索引通常也是 u8，安全
        if (canAssign && match(TOKEN_EQUAL)) {
            expression();
            emitBytes(setOp, (u8)arg);
        } else {
            emitBytes(getOp, (u8)arg);
        }
    } else {
        // [修改] 处理全局变量索引可能 > 255 的情况
        u32 globalArg = identifierConstant(&name);
        
        if (canAssign && match(TOKEN_EQUAL)) {
            expression();
            if (globalArg > 255) {
                 // 需要指令集支持 OP_SET_GLOBAL_LONG
                 error("Global variable index too large.");
            } else {
                emitBytes(OP_SET_GLOBAL, (u8)globalArg);
            }
        } else {
            if (globalArg > 255) {
                // 需要指令集支持 OP_GET_GLOBAL_LONG
                error("Global variable index too large.");
            } else {
                emitBytes(OP_GET_GLOBAL, (u8)globalArg);
            }
        }
    }
}
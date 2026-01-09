// src/core/scanner.c

#include "scanner.h"

// --- Character Classification ---
static INLINE bool isDigit(char c) {
    return c >= '0' && c <= '9';
}
static INLINE bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static INLINE bool isAlphaNum(char c) {
    return isAlpha(c) || isDigit(c);
}

// --- Memory Helpers (SWAR) ---
static INLINE u16 load16(const char* p) {
    u16 result;
    memcpy(&result, p, sizeof(u16));
    return result;
}
static INLINE u32 load32(const char* p) {
    u32 result;
    memcpy(&result, p, sizeof(u32));
    return result;
}

// --- Token Constructors ---
static INLINE Token makeToken(TokenType type, const char* start, const char* current, u32 line) {
    return (Token){
        .start = start,
        .line = line,
        .length = (u16)(current - start),
        .type = (u8)type,
        .flags = TFLAG_NONE
    };
}
static INLINE Token errorToken(const char* message, u32 line) {
    return (Token){
        .start = message,
        .line = line,
        .length = (u16)strlen(message),
        .type = (u8)TOKEN_ERROR,
        .flags = TFLAG_NONE
    };
}

// --- Keyword Trie / Checking ---
static INLINE TokenType checkKeyword(const char* start, i32 length, const char* rest, i32 restLen, TokenType type) {
    if (length != restLen) return TOKEN_IDENTIFIER;
 
    // [优化] SWAR 技术：针对常见长度直接比较整数
    if (restLen == 2) {
        if (load16(start) == load16(rest)) return type;
    } else if (restLen == 3) {
        if (load16(start) == load16(rest) && start[2] == rest[2]) return type;
    } else if (restLen == 4) {
        if (load32(start) == load32(rest)) return type;
    } else if (restLen == 5) {
        if (load32(start) == load32(rest) && start[4] == rest[4]) return type;
    } else if (restLen == 6) {
        if (load32(start) == load32(rest) && load16(start + 4) == load16(rest + 4)) return type;
    } else {
        // 长单词 fallback 到 memcmp
        if (memcmp(start, rest, restLen) == 0) return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(const char* start, i32 length) {
    switch (start[0]) {
        case 'a': return checkKeyword(start + 1, length - 1, "nd", 2, TOKEN_AND);
        case 'b': return checkKeyword(start + 1, length - 1, "reak", 4, TOKEN_BREAK);
        case 'c':
            if (length > 1) {
                switch (start[1]) {
                    case 'l': return checkKeyword(start + 2, length - 2, "ass", 3, TOKEN_CLASS);
                    case 'o': return checkKeyword(start + 2, length - 2, "ntinue", 6, TOKEN_CONTINUE);
                }
            }
            break;
        case 'e':
            if (length > 1) {
                switch (start[1]) {
                    case 'l': return checkKeyword(start + 2, length - 2, "se", 2, TOKEN_ELSE);
                    case 'x': return checkKeyword(start + 2, length - 2, "cept", 4, TOKEN_EXCEPT);
                }
            }
            break;
        case 'f':
            if (length > 1) {
                switch (start[1]) {
                    case 'a': return checkKeyword(start + 2, length - 2, "lse", 3, TOKEN_FALSE);
                    case 'o': return checkKeyword(start + 2, length - 2, "r", 1, TOKEN_FOR);
                    case 'u': return checkKeyword(start + 2, length - 2, "n", 1, TOKEN_FUN);
                }
            }
            break;
        case 'i':
            if (length > 1) {
                switch (start[1]) {
                    case 'f': return checkKeyword(start + 2, length - 2, "", 0, TOKEN_IF);
                    case 'n': return checkKeyword(start + 2, length - 2, "", 0, TOKEN_IN);
                }
            }
            break;
        case 'l': return checkKeyword(start + 1, length - 1, "am", 2, TOKEN_LAM);
        case 'n': return checkKeyword(start + 1, length - 1, "il", 2, TOKEN_NIL);
        case 'o': return checkKeyword(start + 1, length - 1, "r", 1, TOKEN_OR);
        case 'p': return checkKeyword(start + 1, length - 1, "rint", 4, TOKEN_PRINT);
        case 'r': return checkKeyword(start + 1, length - 1, "eturn", 5, TOKEN_RETURN);
        case 's': return checkKeyword(start + 1, length - 1, "uper", 4, TOKEN_SUPER);
        case 't':
            if (length > 1) {
                switch (start[1]) {
                    case 'h': return checkKeyword(start + 2, length - 2, "is", 2, TOKEN_THIS);
                    case 'r':
                        if (checkKeyword(start + 2, length - 2, "ue", 2, TOKEN_TRUE) == TOKEN_TRUE) return TOKEN_TRUE;
                        return checkKeyword(start + 2, length - 2, "y", 1, TOKEN_TRY);
                }
            }
            break;
        case 'v': return checkKeyword(start + 1, length - 1, "ar", 2, TOKEN_VAR);
        case 'w': return checkKeyword(start + 1, length - 1, "hile", 4, TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

// --- Init ---
void initScanner(Scanner* scanner, const char* source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
    scanner->indentStack[0] = 0;
    scanner->indentTop = 0;
    scanner->pendingDedents = 0;
    scanner->isAtStartOfLine = true;
    scanner->parenDepth = 0;
}

// --- Helpers ---
static INLINE bool isAtEnd(Scanner* scanner) {
    return *scanner->current == '\0';
}
static INLINE char advance(Scanner* scanner) {
    return *scanner->current++;
}
static INLINE char peek(Scanner* scanner) {
    return *scanner->current;
}
static INLINE char peekNext(Scanner* scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}

// --- Main Scanner ---
Token scanToken(Scanner* scanner) {
    // 1. 处理挂起的 DEDENT (最高优先级)
    if (scanner->pendingDedents > 0) {
        scanner->pendingDedents--;
        // DEDENT 属于上一行的逻辑结束，不改变 isAtStartOfLine 状态
        return makeToken(TOKEN_DEDENT, scanner->current, scanner->current, scanner->line);
    }
    // 2. 状态机：跳过空白、处理注释、计算缩进
    for (;;) {
        // --- 行首缩进逻辑 ---
        if (scanner->isAtStartOfLine) {
            scanner->isAtStartOfLine = false; // 立即重置，避免死循环
           
            const char* indentStart = scanner->current;
            u16 indent = 0;
            // 快速跳过空格和制表符
            while (*scanner->current == ' ' || *scanner->current == '\t') {
                if (*scanner->current == '\t') indent += 4;
                else indent += 1;
                scanner->current++;
            }
            char c = *scanner->current;
            // 如果是空行、换行或注释，忽略本次缩进计算，留给后续逻辑处理
            if (c != '\n' && c != '\r' && c != '#' && c != '\0') {
                // -> 有效代码行，进行缩进层级检查
                u16 currentIndent = scanner->indentStack[scanner->indentTop];
               
                if (indent > currentIndent) {
                    if (scanner->indentTop >= MAX_INDENT_STACK - 1)
                        return errorToken("Too much indentation.", scanner->line);
                   
                    scanner->indentStack[++scanner->indentTop] = indent;
                    return makeToken(TOKEN_INDENT, indentStart, scanner->current, scanner->line);
                } else if (indent < currentIndent) {
                    // 计算需要产生多少个 DEDENT
                    while (scanner->indentTop > 0 && scanner->indentStack[scanner->indentTop] > indent) {
                        scanner->pendingDedents++;
                        scanner->indentTop--;
                    }
                   
                    if (scanner->indentStack[scanner->indentTop] != indent) {
                        return errorToken("Indentation error: unaligned level.", scanner->line);
                    }
                   
                    // 返回第一个 DEDENT，剩余的存入 pendingDedents
                    scanner->pendingDedents--;
                    return makeToken(TOKEN_DEDENT, scanner->current, scanner->current, scanner->line);
                }
                // indent == currentIndent: 正常，继续执行下面的扫描逻辑
            }
        }
        // --- 普通空白与注释处理 ---
        char c = *scanner->current;
       
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
                scanner->current++;
                continue;
           
            case '#':
                // [优化] 使用 strchr 快速定位行尾，瞬间跳过注释
                {
                    const char* newline = strchr(scanner->current, '\n');
                    if (newline) {
                        scanner->current = newline; // 停在 \n 上，交由下一次循环处理
                    } else {
                        // EOF
                        scanner->current += strlen(scanner->current);
                    }
                }
                continue;
           
            case '\n':
                scanner->line++;
                scanner->current++;
                Token newlineTok = makeToken(TOKEN_NEWLINE, scanner->current - 1, scanner->current, scanner->line - 1);
                if (scanner->parenDepth == 0) {
                    scanner->isAtStartOfLine = true;
                    return newlineTok;
                } else {
                    newlineTok.flags |= TFLAG_SUPPRESSED_NEWLINE;
                    continue;
                }
               
            default:
                // 遇到非空白字符，跳出循环进入 Token 解析
                goto scan_start;
        }
    }
scan_start:;
    const char* start = scanner->current;
    char c = advance(scanner);
    // 3. 标识符 & 关键字
    if (isAlpha(c)) {
        while (isAlphaNum(peek(scanner))) {
            advance(scanner);
        }
       
        i32 length = (i32)(scanner->current - start);
        // [优化] 快速过滤短变量名，避免无意义的 Trie 查找
        TokenType type = TOKEN_IDENTIFIER;
        if (length >= 2) {
             type = identifierType(start, length);
        }
        Token tok = makeToken(type, start, scanner->current, scanner->line);
        if (type == TOKEN_IDENTIFIER && length < 4) {
            tok.flags |= TFLAG_SHORT_IDENT;
        }
        return tok;
    }
    // 4. 数字
    if (isDigit(c)) {
        bool isFloat = false;
        while (isDigit(peek(scanner))) {
            advance(scanner);
        }
        if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
            isFloat = true;
            advance(scanner); // 消耗 '.'
            while (isDigit(peek(scanner))) {
                advance(scanner);
            }
        }
        Token tok = makeToken(TOKEN_NUMBER, start, scanner->current, scanner->line);
        if (isFloat) tok.flags |= TFLAG_IS_FLOAT;
        return tok;
    }
    // 5. 符号与字面量
   
    // 辅助宏，减少重复代码
    #define MAKE_TOKEN(type) \
        return makeToken(type, start, scanner->current, scanner->line)
 
    #define MAKE_TOKEN_MATCH(expected, type_if, type_else) \
        if (peek(scanner) == expected) { \
            advance(scanner); \
            return makeToken(type_if, start, scanner->current, scanner->line); \
        } else { \
            return makeToken(type_else, start, scanner->current, scanner->line); \
        }
    switch (c) {
        case '(': scanner->parenDepth++; MAKE_TOKEN(TOKEN_LEFT_PAREN);
        case ')': if (scanner->parenDepth > 0) scanner->parenDepth--; MAKE_TOKEN(TOKEN_RIGHT_PAREN);
        case '[': scanner->parenDepth++; MAKE_TOKEN(TOKEN_LEFT_BRACKET);
        case ']': if (scanner->parenDepth > 0) scanner->parenDepth--; MAKE_TOKEN(TOKEN_RIGHT_BRACKET);
        case '{': scanner->parenDepth++; MAKE_TOKEN(TOKEN_LEFT_BRACE);
        case '}': if (scanner->parenDepth > 0) scanner->parenDepth--; MAKE_TOKEN(TOKEN_RIGHT_BRACE);
        case ':': MAKE_TOKEN(TOKEN_COLON);
        case ';': MAKE_TOKEN(TOKEN_SEMICOLON);
        case ',': MAKE_TOKEN(TOKEN_COMMA);
        case '.': MAKE_TOKEN(TOKEN_DOT);
        case '-': MAKE_TOKEN(TOKEN_MINUS);
        case '+': MAKE_TOKEN(TOKEN_PLUS);
        case '/': MAKE_TOKEN(TOKEN_SLASH);
        case '*': MAKE_TOKEN(TOKEN_STAR);
     
        case '!': MAKE_TOKEN_MATCH('=', TOKEN_BANG_EQUAL, TOKEN_BANG);
        case '=': MAKE_TOKEN_MATCH('=', TOKEN_EQUAL_EQUAL, TOKEN_EQUAL);
        case '<': MAKE_TOKEN_MATCH('=', TOKEN_LESS_EQUAL, TOKEN_LESS);
        case '>': MAKE_TOKEN_MATCH('=', TOKEN_GREATER_EQUAL, TOKEN_GREATER);
     
        case '"': {
            // [极致优化] 字符串快速扫描
            // 利用 strpbrk 快速跳过普通字符，大幅减少循环次数
            bool hasEscapes = false;
            for (;;) {
                // 查找下一个: 引号、转义符 或 换行符
                const char* next = strpbrk(scanner->current, "\"\\\n");
               
                if (next == NULL) {
                     return errorToken("Unterminated string.", scanner->line);
                }
               
                // 将 current 推进到特殊字符处
                scanner->current = next;
                char special = *scanner->current;
               
                if (special == '\n') {
                    scanner->line++;
                    scanner->current++;
                } else if (special == '\\') {
                    hasEscapes = true;
                    scanner->current++; // 跳过 \
                    if (!isAtEnd(scanner)) scanner->current++; // 跳过转义后的字符
                } else if (special == '"') {
                    scanner->current++; // 消耗结束引号
                    Token tok = makeToken(TOKEN_STRING, start, scanner->current, scanner->line);
                    if (hasEscapes) tok.flags |= TFLAG_HAS_ESCAPES;
                    return tok;
                }
            }
        }
       
        // [修复] 处理 EOF 情况 (防止 errorToken)
        case '\0': {
             // 此时 current 已指向 \0 之后 (因为 advance)，需回退
             scanner->current--;
            
             // EOF 处的自动 DEDENT 处理
             if (scanner->indentTop > 0) {
                 scanner->indentTop--; // 弹出一层
                 scanner->pendingDedents = scanner->indentTop; // 剩余的待处理
                 scanner->indentTop = 0; // 清空栈，防止死循环
                 return makeToken(TOKEN_DEDENT, start, scanner->current, scanner->line);
             }
             return makeToken(TOKEN_EOF, start, scanner->current, scanner->line);
        }
    }
    return errorToken("Unexpected character.", scanner->line);
}
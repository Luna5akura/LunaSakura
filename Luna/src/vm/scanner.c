// src/vm/scanner.c

#include "scanner.h"
// --- Character Attribute Table ---
// 使用运行时初始化以保证标准 C 兼容性，避免 GCC 扩展语法
static u8 charAttrs[256];
static bool charAttrsInitialized = false;
#define ATTR_ALPHA 1
#define ATTR_DIGIT 2
#define IS_ALPHA(c) (charAttrs[(u8)(c)] & ATTR_ALPHA)
#define IS_DIGIT(c) (charAttrs[(u8)(c)] & ATTR_DIGIT)
#define IS_ALPHANUM(c) (charAttrs[(u8)(c)] & (ATTR_ALPHA | ATTR_DIGIT))
static void initCharAttrs() {
    if (charAttrsInitialized) return;
    memset(charAttrs, 0, sizeof(charAttrs));
    for (int i = 'a'; i <= 'z'; i++) charAttrs[i] |= ATTR_ALPHA;
    for (int i = 'A'; i <= 'Z'; i++) charAttrs[i] |= ATTR_ALPHA;
    charAttrs['_'] |= ATTR_ALPHA;
    for (int i = '0'; i <= '9'; i++) charAttrs[i] |= ATTR_DIGIT;
    charAttrsInitialized = true;
}
// --- Memory Helpers ---
static inline u16 load16(const char* p) {
    u16 result;
    memcpy(&result, p, sizeof(u16));
    return result;
}
static inline u32 load32(const char* p) {
    u32 result;
    memcpy(&result, p, sizeof(u32));
    return result;
}
// --- Token Constructors ---
static inline Token makeToken(TokenType type, const char* start, const char* current, u32 line) {
    return (Token){
        .start = start,
        .line = line,
        .length = (u16)(current - start),
        .type = (u8)type,
        .padding = 0
    };
}
static inline Token errorToken(const char* message, u32 line) {
    return (Token){
        .start = message,
        .line = line,
        .length = (u16)strlen(message),
        .type = (u8)TOKEN_ERROR,
        .padding = 0
    };
}
// --- Keyword Trie / Checking ---
// 使用 SWAR (SIMD Within A Register) 技术快速比较短字符串
static inline TokenType checkKeyword(const char* start, i32 length, const char* rest, i32 restLen, TokenType type) {
    if (length != restLen) return TOKEN_IDENTIFIER;
   
    // 优化：针对常见长度直接比较，避免 memcmp 调用开销
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
        case 'e': return checkKeyword(start + 1, length - 1, "lse", 3, TOKEN_ELSE);
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
        case 'l': return checkKeyword(start + 1, length - 1, "am", 2, TOKEN_LAM);  // 新增: lam
        case 'n': return checkKeyword(start + 1, length - 1, "il", 2, TOKEN_NIL);
        case 'o': return checkKeyword(start + 1, length - 1, "r", 1, TOKEN_OR);
        case 'p': return checkKeyword(start + 1, length - 1, "rint", 4, TOKEN_PRINT);
        case 'r': return checkKeyword(start + 1, length - 1, "eturn", 5, TOKEN_RETURN);
        case 's': return checkKeyword(start + 1, length - 1, "uper", 4, TOKEN_SUPER);
        case 't':
            if (length > 1) {
                switch (start[1]) {
                    case 'h': return checkKeyword(start + 2, length - 2, "is", 2, TOKEN_THIS);
                    case 'r': return checkKeyword(start + 2, length - 2, "ue", 2, TOKEN_TRUE);
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
    initCharAttrs(); // 确保查找表已初始化
   
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
    scanner->indentStack[0] = 0;
    scanner->indentTop = 0;
    scanner->pendingDedents = 0;
    scanner->isAtStartOfLine = true;
    scanner->parenDepth = 0;
}
// --- Main Scanner Loop ---
static inline bool isAtEnd(Scanner* scanner) {
    return *scanner->current == '\0';
}
static inline char advance(Scanner* scanner) {
    scanner->current++;
    return scanner->current[-1];
}
static inline char peek(Scanner* scanner) {
    return *scanner->current;
}
static inline char peekNext(Scanner* scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}
Token scanToken(Scanner* scanner) {
    // 1. 处理挂起的 DEDENT
    if (scanner->pendingDedents > 0) {
        scanner->pendingDedents--;
        // 关键修复：DEDENT 产生时，逻辑上该行内容已开始，不再处于行首
        // 防止下次循环再次计算缩进导致错误
        scanner->isAtStartOfLine = false;
        return makeToken(TOKEN_DEDENT, scanner->current, scanner->current, scanner->line);
    }
    // 2. 状态循环 (跳过空格、处理缩进、注释、换行)
    const char* start;
    bool indentWait = false; // 标记是否检测到了有效缩进
    for (;;) {
        // --- 行首缩进处理逻辑 ---
        if (scanner->isAtStartOfLine) {
            const char* indentStart = scanner->current;
            u16 indent = 0;
           
            // 计算缩进量
            while (peek(scanner) == ' ' || peek(scanner) == '\t') {
                if (peek(scanner) == '\t') indent += 4;
                else indent += 1;
                advance(scanner);
            }
            // 如果是空行或注释行，忽略缩进，继续寻找下一行
            if (peek(scanner) == '\n' || peek(scanner) == '#' || peek(scanner) == '\r') {
                // 不重置 isAtStartOfLine，继续下一轮循环
            } else if (!isAtEnd(scanner)) {
                // -> 遇到有效代码，检查缩进层级
               
                u16 currentIndent = scanner->indentStack[scanner->indentTop];
                if (indent > currentIndent) {
                    if (scanner->indentTop >= MAX_INDENT_STACK - 1)
                        return errorToken("Too much indentation.", scanner->line);
                   
                    scanner->indentStack[++scanner->indentTop] = indent;
                    // 发送 INDENT，并标记不再处于行首，防止死循环
                    scanner->isAtStartOfLine = false;
                    return makeToken(TOKEN_INDENT, indentStart, scanner->current, scanner->line);
                }
                else if (indent < currentIndent) {
                    // 计算需要回退多少层
                    while (scanner->indentTop > 0 && scanner->indentStack[scanner->indentTop] > indent) {
                        scanner->pendingDedents++;
                        scanner->indentTop--;
                    }
                   
                    if (scanner->indentStack[scanner->indentTop] != indent) {
                        return errorToken("Indentation error: unaligned level.", scanner->line);
                    }
                   
                    // 发送第一个 DEDENT
                    scanner->pendingDedents--;
                    scanner->isAtStartOfLine = false;
                    return makeToken(TOKEN_DEDENT, scanner->current, scanner->current, scanner->line);
                }
               
                // indent == currentIndent: 正常，标记行首处理结束，进入普通扫描
                scanner->isAtStartOfLine = false;
            }
        }
        // --- 普通空白处理 ---
        char c = peek(scanner);
       
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(scanner);
            continue;
        }
       
        if (c == '#') {
            while (peek(scanner) != '\n' && !isAtEnd(scanner)) {
                advance(scanner);
            }
            continue;
        }
       
        if (c == '\n') {
            scanner->line++;
            advance(scanner);
           
            // 如果在括号内，或者文件开头，换行被视为普通空白
            if (scanner->parenDepth > 0) continue;
           
            // 标记下一轮循环需要检查行首缩进
            scanner->isAtStartOfLine = true;
           
            // 只有当不仅是连续空行时才返回 NEWLINE (简化逻辑：解析器会处理多余 NEWLINE)
            return makeToken(TOKEN_NEWLINE, scanner->current - 1, scanner->current, scanner->line - 1);
        }
        break; // 找到有效字符
    }
    start = scanner->current;
   
    if (isAtEnd(scanner)) {
        // EOF 处理：如果还有缩进，自动补全 DEDENT
        if (scanner->indentTop > 0) {
            scanner->pendingDedents = scanner->indentTop;
            scanner->indentTop = 0;
            // 递归调用（或直接返回第一个）以处理剩余 DEDENT
            return scanToken(scanner);
        }
        return makeToken(TOKEN_EOF, start, scanner->current, scanner->line);
    }
    char c = advance(scanner);
    // 3. 标识符 & 关键字
    if (IS_ALPHA(c)) {
        while (IS_ALPHANUM(peek(scanner))) {
            advance(scanner);
        }
        TokenType type = identifierType(start, (i32)(scanner->current - start));
        return makeToken(type, start, scanner->current, scanner->line);
    }
    // 4. 数字
    if (IS_DIGIT(c)) {
        while (IS_DIGIT(peek(scanner))) {
            advance(scanner);
        }
        if (peek(scanner) == '.' && IS_DIGIT(peekNext(scanner))) {
            advance(scanner); // '.'
            while (IS_DIGIT(peek(scanner))) {
                advance(scanner);
            }
        }
        return makeToken(TOKEN_NUMBER, start, scanner->current, scanner->line);
    }
    // 5. 符号
    // 宏定义简化
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
        case '[': scanner->parenDepth++; MAKE_TOKEN(TOKEN_LEFT_BRACKET); // 列表通常也忽略换行
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
            // [修复] 正确处理转义字符和字符串结束
            while (peek(scanner) != '"' && !isAtEnd(scanner)) {
                if (peek(scanner) == '\n') {
                    scanner->line++;
                }
               
                if (peek(scanner) == '\\') {
                    advance(scanner); // 消耗 '\'
                    // 如果不是 EOF，消耗转义后的字符（哪怕是 '"' 也不作为结束符）
                    if (!isAtEnd(scanner)) {
                        advance(scanner);
                    }
                } else {
                    advance(scanner);
                }
            }
           
            if (isAtEnd(scanner)) {
                return errorToken("Unterminated string.", scanner->line);
            }
           
            advance(scanner); // 消耗闭合的 '"'
            return makeToken(TOKEN_STRING, start, scanner->current, scanner->line);
        }
    }
    return errorToken("Unexpected character.", scanner->line);
}
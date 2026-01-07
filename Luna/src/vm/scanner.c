// src/vm/scanner.c

#include <stdio.h>
#include <string.h>
#include "scanner.h"
// --- Character Attribute Table (Lookup Table) ---
static const u8 charAttrs[256] = {
    ['\t'] = 4, ['\r'] = 4, [' '] = 4,
    ['a' ... 'z'] = 1,
    ['A' ... 'Z'] = 1,
    ['_'] = 1,
    ['0' ... '9'] = 2
};
#define IS_ALPHA(c) (charAttrs[(u8)(c)] & 1)
#define IS_DIGIT(c) (charAttrs[(u8)(c)] & 2)
#define IS_SPACE(c) (charAttrs[(u8)(c)] & 4)
#define IS_ALPHANUM(c) (charAttrs[(u8)(c)] & 3)
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
// --- Keyword Check ---
static inline TokenType checkKeyword(const char* start, i32 length, const char* rest, i32 restLen, TokenType type) {
    if (length != restLen) return TOKEN_IDENTIFIER;
    if (restLen == 2) {
        if (load16(start) == load16(rest)) return type;
        return TOKEN_IDENTIFIER;
    }
    if (restLen == 3) {
        if (load16(start) == load16(rest) && start[2] == rest[2]) return type;
        return TOKEN_IDENTIFIER;
    }
    if (restLen == 4) {
        if (load32(start) == load32(rest)) return type;
        return TOKEN_IDENTIFIER;
    }
    if (restLen == 5) {
        if (load32(start) == load32(rest) && start[4] == rest[4]) return type;
        return TOKEN_IDENTIFIER;
    }
    if (restLen == 6) {
        if (load32(start) == load32(rest) && load16(start + 4) == load16(rest + 4)) return type;
        return TOKEN_IDENTIFIER;
    }
    if (memcmp(start, rest, restLen) == 0) return type;
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
                    case 'n': return checkKeyword(start + 2, length - 2, "", 0, TOKEN_IN); // [新增] 识别 'in'
                }
            }
            break;
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
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
 
    // 初始化缩进状态
    scanner->indentStack[0] = 0;
    scanner->indentTop = 0;
    scanner->pendingDedents = 0;
    scanner->isAtStartOfLine = true;
    scanner->parenDepth = 0;
}
// --- Main Scanner Loop ---
Token scanToken(Scanner* scanner) {
    // 1. 处理挂起的 DEDENT (当缩进减少多层时)
    if (scanner->pendingDedents > 0) {
        scanner->pendingDedents--;
        return makeToken(TOKEN_DEDENT, scanner->current, scanner->current, scanner->line);
    }
    // 2. 状态循环 (跳过空格、注释，处理行首)
    const char* start;
    const char* current = scanner->current;
    u32 line = scanner->line;
    for (;;) {
        // 如果在行首，计算缩进
        if (scanner->isAtStartOfLine) {
            int indent = 0;
            // 计算空格 (Tab算4个空格，建议禁止混用)
            while (*current == ' ' || *current == '\t') {
                if (*current == '\t') indent += 4;
                else indent += 1;
                current++;
            }
            // 如果遇到空行或注释行，忽略该行，重置行首状态
            if (*current == '\n' || *current == '#' || *current == '\r') {
                // 不做任何操作，让后面的逻辑去处理换行符，或者直接跳过
            } else if (*current != '\0') {
                // 有效代码行开始，检查缩进
                scanner->isAtStartOfLine = false;
             
                int currentIndent = scanner->indentStack[scanner->indentTop];
                if (indent > currentIndent) {
                    if (scanner->indentTop >= MAX_INDENT_STACK - 1)
                        return errorToken("Too much indentation.", line);
                    scanner->indentStack[++scanner->indentTop] = indent;
                    scanner->current = current; // 更新指针，因为消耗了空格
                    return makeToken(TOKEN_INDENT, current, current, line);
                }
                else if (indent < currentIndent) {
                    // 计算需要退几层
                    while (scanner->indentTop > 0 && scanner->indentStack[scanner->indentTop] > indent) {
                        scanner->pendingDedents++;
                        scanner->indentTop--;
                    }
                    if (scanner->indentStack[scanner->indentTop] != indent) {
                        return errorToken("Indentation error.", line);
                    }
                    // 发送第一个 DEDENT
                    scanner->pendingDedents--;
                    scanner->current = current;
                    return makeToken(TOKEN_DEDENT, current, current, line);
                }
                // indent == currentIndent: 正常同级代码，什么都不做
            }
        }
        // 正常的跳过空格 (非行首)
        char c = *current;
        if (c == ' ' || c == '\t' || c == '\r') {
            current++;
            continue;
        }
        // 处理注释
        if (c == '#') {
            while (*current != '\n' && *current != '\0') {
                current++;
            }
            continue;
        }
        // 处理换行
        if (c == '\n') {
            line++;
            current++;
         
            // 如果在括号内，或者这行本身就是空的（isAtStartOfLine仍为true说明没遇到代码），则忽略换行
            if (scanner->parenDepth > 0) {
                // 括号内换行等同于空格
                continue;
            }
         
            // 预读：如果下一行也是空行或注释，我们这里可以继续循环，不发出 NEWLINE
            // 但为了简单，我们总是进入下一轮循环，让 isAtStartOfLine 逻辑决定
            scanner->isAtStartOfLine = true;
         
            // 只有当上一行也是代码时（非连续换行），才发出 NEWLINE
            // 这里简化策略：每次换行都重置为行首，下一轮循环如果遇到代码则发出 indent check
            // 我们需要发出 TOKEN_NEWLINE 给 parser 来结束语句
         
            // 优化：只有当该行包含非空内容后遇到的换行才算有效，
            // 但因为我们已经在循环里跳过了前面的空行，所以这里遇到的 '\n' 通常是语句结束。
            // 除非... 它是文件的第一个字符
            if (current - 1 == scanner->start) {
                // 文件开头的换行，忽略
                continue;
            }
            scanner->current = current;
            scanner->line = line;
            return makeToken(TOKEN_NEWLINE, current - 1, current, line - 1);
        }
        break; // 遇到有效字符
    }
    // 更新 scanner 状态
    scanner->current = current;
    scanner->line = line;
    start = current;
    if (*current == '\0') {
        // EOF 处理：如果还有缩进，自动补全 DEDENT
        if (scanner->indentTop > 0) {
            scanner->pendingDedents = scanner->indentTop;
            scanner->indentTop = 0;
            // 递归自己来发出 DEDENT
            return scanToken(scanner);
        }
        return makeToken(TOKEN_EOF, start, current, line);
    }
    char c = *current++;
    scanner->current = current; // 消耗字符
    // 3. 标识符 & 关键字
    if (IS_ALPHA(c)) {
        while (IS_ALPHANUM(*scanner->current)) {
            scanner->current++;
        }
        TokenType type = identifierType(start, (i32)(scanner->current - start));
        return makeToken(type, start, scanner->current, line);
    }
    // 4. 数字
    if (IS_DIGIT(c)) {
        while (IS_DIGIT(*scanner->current)) {
            scanner->current++;
        }
        if (*scanner->current == '.' && IS_DIGIT(scanner->current[1])) {
            scanner->current++; // Consume '.'
            while (IS_DIGIT(*scanner->current)) {
                scanner->current++;
            }
        }
        return makeToken(TOKEN_NUMBER, start, scanner->current, line);
    }
    // 5. 符号
    char next = *scanner->current;
 
    // 宏定义简化
    #define MAKE_TOKEN(type) \
        do { return makeToken(type, start, scanner->current, line); } while(0)
    #define MAKE_TOKEN_MATCH(expected, type_if, type_else) \
        do { if (next == expected) scanner->current++; \
             return makeToken(next == expected ? type_if : type_else, \
                              start, scanner->current, line); } while(0)
    switch (c) {
        case '(': scanner->parenDepth++; MAKE_TOKEN(TOKEN_LEFT_PAREN);
        case ')': if (scanner->parenDepth > 0) scanner->parenDepth--; MAKE_TOKEN(TOKEN_RIGHT_PAREN);
        case '[': MAKE_TOKEN(TOKEN_LEFT_BRACKET);
        case ']': MAKE_TOKEN(TOKEN_RIGHT_BRACKET);
        case '{': MAKE_TOKEN(TOKEN_LEFT_BRACE); // [修改] 添加
        case '}': MAKE_TOKEN(TOKEN_RIGHT_BRACE); // [修改] 添加
        case ':': MAKE_TOKEN(TOKEN_COLON); // Python 关键符号
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
            const char* str_ptr = scanner->current;
            for (;;) {
                char sc = *str_ptr;
                if (sc == '"') {
                    str_ptr++;
                    scanner->current = str_ptr;
                    return makeToken(TOKEN_STRING, start, str_ptr, line);
                }
                if (sc == '\0') {
                    return errorToken("Unterminated string.", line);
                }
                if (sc == '\n') line++;
                str_ptr++;
            }
        }
    }
    return errorToken("Unexpected character.", line);
}
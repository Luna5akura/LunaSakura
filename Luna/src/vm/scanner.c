// src/vm/scanner.c

#include <stdio.h>
#include <string.h>
#include "common.h"
#include "scanner.h"

// --- Character Attribute Table (Lookup Table) ---
// Optimization: Replaces multiple branch instructions (if/else if) with a single array access.
// Bit 0 (0x01): Alpha (a-zA-Z_)
// Bit 1 (0x02): Digit (0-9)
// Bit 2 (0x04): Space (Tab, CR, Space) - Newline handled separately for line counting
static const u8 charAttrs[256] = {
    ['\t'] = 4, ['\r'] = 4, [' '] = 4,
    ['a' ... 'z'] = 1,
    ['A' ... 'Z'] = 1,
    ['_'] = 1,
    ['0' ... '9'] = 2
};
// charAttrs[0] is implicitly 0, so loop stops at null terminator automatically.
#define IS_ALPHA(c) (charAttrs[(u8)(c)] & 1)
#define IS_DIGIT(c) (charAttrs[(u8)(c)] & 2)
#define IS_SPACE(c) (charAttrs[(u8)(c)] & 4)
#define IS_ALPHANUM(c) (charAttrs[(u8)(c)] & 3)

// --- Memory Helpers (Unaligned Access) ---
// Uses memcpy to allow the compiler to emit unaligned MOV instructions safely.
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

// --- Keyword Check (SWAR Optimization) ---
// Compares strings as integers (SIMD Within A Register) for speed.
// Prerequisite: 'restLen' is known at compile time, allowing dead code elimination.
static inline TokenType checkKeyword(const char* start, int length, const char* rest, int restLen, TokenType type) {
    // 1. Length check (Cheapest)
    if (length != restLen) return TOKEN_IDENTIFIER;
    // 2. Content check (Integer comparison)
   
    // 2-byte: "if", "or", "do"
    if (restLen == 2) {
        if (load16(start) == load16(rest)) return type;
        return TOKEN_IDENTIFIER;
    }
    // 3-byte: "and", "var", "for", "nil"
    if (restLen == 3) {
        if (load16(start) == load16(rest) && start[2] == rest[2]) return type;
        return TOKEN_IDENTIFIER;
    }
    // 4-byte: "true", "this", "else"
    if (restLen == 4) {
        if (load32(start) == load32(rest)) return type;
        return TOKEN_IDENTIFIER;
    }
    // 5-byte: "while", "false", "class", "print", "super"
    if (restLen == 5) {
        if (load32(start) == load32(rest) && start[4] == rest[4]) return type;
        return TOKEN_IDENTIFIER;
    }
    // 6-byte: "return"
    if (restLen == 6) {
        // Checks first 4 bytes + last 2 bytes
        if (load32(start) == load32(rest) && load16(start + 4) == load16(rest + 4)) return type;
        return TOKEN_IDENTIFIER;
    }
    // Fallback for rare longer keywords
    if (memcmp(start, rest, restLen) == 0) return type;
    return TOKEN_IDENTIFIER;
}

// Trie-based dispatch
static TokenType identifierType(const char* start, int length) {
    switch (start[0]) {
        case 'a': return checkKeyword(start + 1, length - 1, "nd", 2, TOKEN_AND);
        case 'c': return checkKeyword(start + 1, length - 1, "lass", 4, TOKEN_CLASS);
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
        case 'i': return checkKeyword(start + 1, length - 1, "f", 1, TOKEN_IF);
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

// --- Main Scanner Loop ---
Token scanToken(Scanner* scanner) {
    // Cache global state in local variables (registers) for the loop duration
    const char* current = scanner->current;
    u32 line = scanner->line;
    const char* start;
    // 1. Skip Whitespace & Comments
skip_whitespace:
    start = current;
    u8 c = *current;
   
    // Check lookup table for Space/Tab/CR
    if (IS_SPACE(c)) {
        current++;
        goto skip_whitespace;
    }
   
    if (c == '\n') {
        line++;
        current++;
        goto skip_whitespace;
    }
   
    if (c == '#') {
        // Comment: Read until newline or EOF
        while (*current != '\n' && *current != '\0') {
            current++;
        }
        goto skip_whitespace;
    }
    // 2. Token Start
    start = current;
   
    if (*current == '\0') {
        scanner->current = current;
        scanner->line = line;
        return makeToken(TOKEN_EOF, start, current, line);
    }
    c = *current++;
    // 3. Identifiers & Keywords
    if (IS_ALPHA(c)) {
        while (IS_ALPHANUM(*current)) {
            current++;
        }
       
        TokenType type = identifierType(start, (int)(current - start));
       
        scanner->current = current;
        scanner->line = line;
        return makeToken(type, start, current, line);
    }
    // 4. Numbers
    if (IS_DIGIT(c)) {
        while (IS_DIGIT(*current)) {
            current++;
        }
        if (*current == '.' && IS_DIGIT(current[1])) {
            current++; // Consume '.'
            while (IS_DIGIT(*current)) {
                current++;
            }
        }
        scanner->current = current;
        scanner->line = line;
        return makeToken(TOKEN_NUMBER, start, current, line);
    }
    // 5. Symbols
    // Prepare for lookahead (1 char)
    char next = *current;
   
    // Macro to reduce boilerplate for single-char tokens
    #define MAKE_TOKEN(type) \
        do { scanner->current = current; scanner->line = line; \
             return makeToken(type, start, current, line); } while(0)
    // Macro for two-char tokens (e.g., !=, ==)
    #define MAKE_TOKEN_MATCH(expected, type_if, type_else) \
        do { if (next == expected) current++; \
             scanner->current = current; scanner->line = line; \
             return makeToken(next == expected ? type_if : type_else, \
                              start, current, line); } while(0)
    switch (c) {
        case '(': MAKE_TOKEN(TOKEN_LEFT_PAREN);
        case ')': MAKE_TOKEN(TOKEN_RIGHT_PAREN);
        case '{': MAKE_TOKEN(TOKEN_LEFT_BRACE);
        case '}': MAKE_TOKEN(TOKEN_RIGHT_BRACE);
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
            const char* str_ptr = current;
            for (;;) {
                char sc = *str_ptr;
                if (sc == '"') {
                    str_ptr++; // Consume closing quote
                    scanner->current = str_ptr;
                    scanner->line = line;
                    return makeToken(TOKEN_STRING, start, str_ptr, line);
                }
                if (sc == '\0') {
                    scanner->current = str_ptr;
                    scanner->line = line;
                    return errorToken("Unterminated string.", line);
                }
                if (sc == '\n') line++;
                str_ptr++;
            }
        }
    }
    #undef MAKE_TOKEN
    #undef MAKE_TOKEN_MATCH
    scanner->current = current;
    scanner->line = line;
    return errorToken("Unexpected character.", line);
}

void initScanner(Scanner* scanner, const char* source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
}
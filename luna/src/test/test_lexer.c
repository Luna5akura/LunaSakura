// test_lexer.c

#include <ast.h>

#include "common.h"
#include "lexer.h"
#include "stdio.h"
#include "mem.h"

void test_lexer(const char* source_code) {
    LexerState* lexer = create_lexer_from_string(source_code);
    Token token;

    pprintf("Testing lexer with input: \"%s\"\n", source_code);
    while ((token = lexer_next_token(lexer)).type != TOKEN_EOF) {
        pprintf("Token: Type=%d, Text=\"%s\"\n", token.type, token.text);
        if (token.text) {
            mfree(token.text);
        }
    }
    mfree(lexer);
}

int main() {
    // const char* source_code = "if (x < 10) x = x + 1;";
    // test_lexer(source_code);

    // const char* source_code2 = "var total = 5 * (3 + 2);";
    // test_lexer(source_code2);

    // const char* source_code3 = "if x < 10:\n    x = x + 1\n";
    // test_lexer(source_code3);

    const char* source_code12 = "if x>0:\n   y=x\n   if y>10:\n      print(\"too large number!\")\nelse:\n   y=-x\n";
    test_lexer(source_code12);

    const char* source_code14 = "for i in range(10):\n    print(i)\nprint(\"finish\")\n";
    test_lexer(source_code14);

    return 0;
}
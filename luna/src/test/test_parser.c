// test_parser.c

#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "stdio.h"
#include "mem.h"

void test_parser(const char* source_code) {
    LexerState* lexer = create_lexer_from_string(source_code);
    Parser* parser = create_parser(lexer);

    pprintf("Testing parser with input: \"%s\"\n", source_code);

    Node* ast = parse_program(parser);
    if (ast) {
        pprintf("Parse success. AST:\n");
        print_node(ast);
        pprintf("\n");
        free_node(ast);
    } else {
        pprintf("Parse failed.\n");
    }

    free_parser(parser);
    mfree(lexer);
}

int main() {
    // const char* source_code = "x = 5 + 3";
    // test_parser(source_code);
    //
    // const char* source_code2 = "if x>0:\n    y=x\n";
    // test_parser(source_code2);
    //
    // const char* source_code3 = "a * b + c / d - e";
    // test_parser(source_code3);
    //
    // const char* source_code4 = "x + y > z - w";
    // test_parser(source_code4);
    //
    // const char* source_code5 = "max(a, b) * c";
    // test_parser(source_code5);
    //
    // const char* source_code6 = "if x < 10:\n    x = x + 1\n";
    // test_parser(source_code6);
    //
    // const char* source_code7 = "foo()";
    // test_parser(source_code7);
    //
    // const char* source_code8 = "baz(a, b + c, foo(x))";
    // test_parser(source_code8);
    //
    // const char* source_code9 = "if x > 0:\n   y = x\n   if y > 10:\n      print(z)\n";
    // test_parser(source_code9);

    // const char* source_code10 = "if x>0:\n   y=x\nelse:\n   y=-x\n";
    // test_parser(source_code10);

    // const char* source_code11 = "if x>0:\n   y=x\n   if y>10:\n      print(z)\nelse:\n   y=-x\n";
    // test_parser(source_code11);

    // const char* source_code12 = "if x>0:\n   y=x\n   if y>10:\n      print(\"Too Large Number!\")\nelse:\n   y=-x\n";
    // test_parser(source_code12);
    //
    // const char* source_code13 = "while i<0:\n    i=i+1\n    print(i)\nprint(\"Finish\")\n";
    // test_parser(source_code13);
    //
    // const char* source_code14 = "for i in range(10):\n    print(i)\nprint(\"Finish\")\n";
    // test_parser(source_code14);

    const char* source_code15 = "def foo():\n    print(1)\ndef add(a, b):\n    print(a + b)\n    return a+b\nadd(1, 2)\n";
    test_parser(source_code15);

    const char* source_code16 = "a = [1, 2 + 3, print(3), 4]\n";
    test_parser(source_code16);

    const char* source_code17 = "a[1::]\na[1:2:]\na[1:-2:3]\na[:1:]\na[:-1:2]\na[::1]\na[::]\na[1:]\na[:-2]\na[1:len(b)]\na[-3]";
    test_parser(source_code17);

    return 0;
}
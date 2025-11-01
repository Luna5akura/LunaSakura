// main.c

#include "stdio.h"
#include "mem.h"

#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"

int main(int argc, char** argv) {
  const char* source_code =
    // "a = 0\n"
    // "b = 1\n"
    // "i = 1\n"
    // "while i < 10:\n"
    // "    c = a + b\n"
    // "    a = b\n"
    // "    b = c\n"
    // "    print(b)\n"
    // "    if b > 30:\n"
    // "        print(\"b is greater than 30\")\n"
    // "    else:\n"
    // "        print(\"b is smaller than 30\")\n"
    // "    i = i + 1\n"
    // "print(\"Finish\")";

    // "def add(a, b):\n"
    // "    return a+b\n"
    // "print(add(1, 2))\n"
    // "print(add(88, -23))";

    // "a = [1, 2, 3, 4, 5]\n"
    // "a = \"Hello World!\"\n"
    // "print(a[0])\n"
    // "print(a[-1])\n"
    // "print(a[1:3])\n"
    // "print(a[:4])\n"
    // "print(a[3:])\n"
    // "print(a[::2])\n"
    // "print(a[3::-1])\n"
    // "print(a[3:3])\n"
    // "print(a[3:2])";

    // "a = [1, 2, 3]\n"
    // "for i in a:\n"
    // "    print('First',i)\n"
    // "b = \"Hello World!\"\n"
    // "for i in b[1:6:2]:\n"
    // "    print(i)\n"
    // "for i in range(3):\n"
    // "    print(i)\n"
    // "print()"
    // "for i in range(4, 6):\n"
    // "    print(i)\n"
    // "print()"
    // "for i in range(8, 2, -2):\n"
    // "    print(i)\n";

    "a = input('input testing>>>')\n"
    "print(a)";

  LexerState* lexer = create_lexer_from_string(source_code);
  Parser* parser = create_parser(lexer);
  Node* program = parse_program(parser);

  Compiler compiler;
  init_compiler(&compiler);
  compile(program, &compiler);

  VM vm;
  init_vm(&vm);
  InterpretResult result = interpret(&vm, compiler.chunk);

  if (result != INTERPRET_OK) {
    pprintf("Interpretation failed.\n");
  }

  free_vm(&vm);
  free_compiler(&compiler);
  free_node(program);
  free_parser(parser);
  mfree(lexer);

  return 0;
}
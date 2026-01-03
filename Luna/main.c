// main.c
#include <stdio.h>
#include "common.h"
#include "src/vm/chunk.h"
#include "src/vm/vm.h"
#include "src/vm/compiler.h"

Value nativeCreateClip(int argCount, Value* args);
Value nativePreview(int argCount, Value* args); // <--- 新增
Value nativeTrim(int argCount, Value* args); // 声明
Value nativeExport(int argCount, Value* args); // 声明

int main(int argc, char* argv[]) {
    initVM();
    defineNative("Clip", nativeCreateClip);
    defineNative("preview", nativePreview);
    defineNative("trim", nativeTrim); // <--- 注册 trim
    defineNative("export", nativeExport); // <--- 注册

    Chunk chunk;
    initChunk(&chunk);

    const char* source = 
        "var vid = Clip(\"test.mp4\");\n"
        "print \"Trimming video...\";\n"
        "trim(vid, 5, 3); # Start at 5s, len 3s\n"
        "\n"
        "print \"Exporting to output.mp4...\";\n"
        "export(vid, \"output.mp4\");"; 

    printf("Compiling script:\n%s\n\n", source);

    if (compile(source, &chunk)) {
        printf("Running...\n");
        interpret(&chunk);
    }

    freeVM();
    freeChunk(&chunk);
    return 0;
}
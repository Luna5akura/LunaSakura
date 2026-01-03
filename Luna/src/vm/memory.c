// src/vm/memory.c

#include <stdlib.h>
#include "memory.h"
#include "vm.h" // 为了访问全局 vm 里的对象链表

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1); // 内存耗尽
    return result;
}

static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            // 释放字符数组
            reallocate(string->chars, sizeof(char) * (string->length + 1), 0);
            // 释放结构体本身
            reallocate(object, sizeof(ObjString), 0);
            break;
        }
        case OBJ_CLIP: {
            // 将来处理 Clip 的释放
            reallocate(object, sizeof(ObjClip), 0);
            break;
        }
    }
}

void freeObjects() {
    // 暂时假设 vm.objects 是一个链表头（稍后我们在 vm.h 添加）
    // Obj* object = vm.objects;
    // while (object != NULL) {
    //     Obj* next = object->next;
    //     freeObject(object);
    //     object = next;
    // }
}
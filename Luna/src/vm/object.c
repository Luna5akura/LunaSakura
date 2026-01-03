// src/vm/object.c

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "vm.h"

// 申请一个基础对象
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    
    // 以后这里要把对象加入 GC 链表
    // object->next = vm.objects;
    // vm.objects = object;
    
    return object;
}

// 申请一个字符串对象
static ObjString* allocateString(char* chars, int length) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = chars;
    return string;
}

// FNV-1a 哈希算法
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

// 核心：从源代码拷贝字符串到堆内存
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    
    // 1. 先去池子里找，如果找到了直接返回旧的
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
    
    // 2. 没找到，才申请新内存
    char* heapChars = (char*)reallocate(NULL, 0, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    
    ObjString* string = allocateString(heapChars, length);
    string->hash = hash;
    
    // 3. 放入池子
    tableSet(&vm.strings, string, NIL_VAL);
    
    return string;
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_CLIP:
            printf("<clip>");
            break;
        case OBJ_NATIVE: printf("<native fn>"); break;
    }
}

ObjNative* newNative(NativeFn function) {
    ObjNative* native = (ObjNative*)allocateObject(sizeof(ObjNative), OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjClip* newClip(ObjString* path) {
    // 申请内存，类型标记为 OBJ_CLIP
    ObjClip* clip = (ObjClip*)allocateObject(sizeof(ObjClip), OBJ_CLIP);
    
    // 初始化字段
    clip->path = path;
    clip->start_time = 0.0;
    clip->duration = 0.0;
    clip->in_point = 0.0;
    clip->out_point = 0.0;
    clip->layer = 0;
    
    return clip;
}
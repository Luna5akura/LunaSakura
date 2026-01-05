// src/vm/object.c

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#include "engine/timeline.h"


// === 核心分配函数 ===
static Obj* allocateObject(VM* vm, size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(vm, NULL, 0, size);
    object->type = type;
    object->isMarked = false;
    object->next = vm->objects;
    vm->objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
    return object;
}

// === 字符串处理 ===
static ObjString* allocateString(VM* vm, int length) {
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    return string;
}

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* copyString(VM* vm, const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
   
    // 1. 查重
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) return interned;
   
    // 2. 分配
    ObjString* string = allocateString(vm, length);
    if (string == NULL) {
        fprintf(stderr, "Allocation failed for string of length %d.\n", length);
        return NULL;
    }
   
    // 3. 拷贝
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;
   
    // 4. 入池
    if (!tableSet(vm, &vm->strings, string, NIL_VAL)) {
        // Handle failure
    }
   
    return string;
}

ObjString* takeString(VM* vm, char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) {
        // 显式转换 void 消除警告（如果不想修改宏定义）
        (void)reallocate(vm, chars, sizeof(char) * (length + 1), 0);
        return interned;
    }
    
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    
    // 释放传入的 chars
    (void)reallocate(vm, chars, sizeof(char) * (length + 1), 0);

    tableSet(vm, &vm->strings, string, NIL_VAL);
    
    return string;
}

// === 其他对象创建 ===

ObjNative* newNative(VM* vm, NativeFn function) {
    ObjNative* native = (ObjNative*)allocateObject(vm, sizeof(ObjNative), OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjClip* newClip(VM* vm, ObjString* path) {
    ObjClip* clip = (ObjClip*)allocateObject(vm, sizeof(ObjClip), OBJ_CLIP);
   
    clip->path = path;
    clip->start_time = 0.0;
    clip->duration = 0.0;
    clip->in_point = 0.0;
    clip->out_point = 0.0;
    clip->fps = 0.0;
    clip->width = 0;
    clip->height = 0;
    clip->layer = 0;
    clip->default_scale_x = 1.0;
    clip->default_scale_y = 1.0;
    clip->default_x = 0.0;
    clip->default_y = 0.0;
    clip->default_opacity = 1.0; 
   
    return clip;
}

ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps) {
    ObjTimeline* obj = (ObjTimeline*)allocateObject(vm, sizeof(ObjTimeline), OBJ_TIMELINE);
   
    // 现在有了 include "engine/timeline.h"，这里可以正确调用了
    obj->timeline = timeline_create(width, height, fps);
   
    return obj;
}

// === 调试打印 ===
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
           
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
           
        case OBJ_CLIP:
            if (AS_CLIP(value)->path != NULL) {
                printf("<clip \"%s\">", AS_CLIP(value)->path->chars);
            } else {
                printf("<clip>");
            }
            break;
        case OBJ_TIMELINE:
            printf("<timeline %p>", (void*)AS_TIMELINE(value)->timeline);
            break;
    }
}
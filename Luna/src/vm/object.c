// src/vm/object.c

#include <stdio.h>
#include "memory.h"
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
static ObjString* allocateString(VM* vm, i32 length) {
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = (u32)length;
    return string;
}
static u32 hashString(const char* key, i32 length) {
    u32 hash = 2166136261u;
    for (i32 i = 0; i < length; i++) {
        hash ^= (u8)key[i];
        hash *= 16777619;
    }
    return hash;
}
ObjString* copyString(VM* vm, const char* chars, i32 length) {
    u32 hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) return interned;

    ObjString* string = allocateString(vm, length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;

    // [新增] 关键修复：GC 保护
    // 在调用 tableSet 之前，将 string 压入栈中。
    // 如果 tableSet 触发扩容 -> 触发 GC，GC 会在栈上看到这个 string 并标记它。
    push(vm, OBJ_VAL(string)); 
    
    tableSet(vm, &vm->strings, string, NIL_VAL);
    
    // 操作完成后弹出
    pop(vm); 

    return string;
}

ObjString* takeString(VM* vm, char* chars, i32 length) {
    u32 hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) {
        // 显式转换 void 消除警告（如果不想修改宏定义）
        (void)reallocate(vm, chars, sizeof(char) * (length + 1), 0);
        return interned;
    }
   
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = (u32)length;
    string->hash = hash;
   
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
   
    // 释放传入的 chars
    (void)reallocate(vm, chars, sizeof(char) * (length + 1), 0);
    tableSet(vm, &vm->strings, string, NIL_VAL);
   
    return string;
}
ObjList* newList(VM* vm) {
    ObjList* list = (ObjList*)allocateObject(vm, sizeof(ObjList), OBJ_LIST);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return list;
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
    obj->timeline = timeline_create(vm, width, height, fps);
  
    return obj;
}
// === 调试打印 ===
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {

        case OBJ_LIST: {
            ObjList* list = AS_LIST(value);
            printf("[");
            for (u32 i = 0; i < list->count; i++) {
                printValue(list->items[i]);
                if (i < list->count - 1) printf(", ");
            }
            printf("]");
            break;
        }
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
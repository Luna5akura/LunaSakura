// src/vm/object.c

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#include "engine/timeline.h"

// 声明全局 vm（假设在 vm.c 中定义）
extern VM vm;

// === 核心分配函数 ===
// 分配对象头，并将其链接到 VM 的对象链表中 (GC 根链表)
static Obj* allocateObject(size_t size, ObjType type) {
    // 1. 申请内存
    Obj* object = (Obj*)reallocate(&vm, NULL, 0, size);  // 传入 &vm
   
    // 2. 初始化基类字段
    object->type = type;
    object->isMarked = false; // GC 标记位初始化为 false
   
    // 3. 立即链接到 VM (关键：防止对象在未被引用前被 GC 回收)
    object->next = vm.objects;
    vm.objects = object;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
    return object;
}

// === 字符串处理 ===
// 专门为 ObjString 分配内存 (包含柔性数组)
static ObjString* allocateString(int length) {
    // 优化：一次性申请 "结构体头 + 字符串内容 + 结束符" 的连续内存
    // sizeof(ObjString) 不包含柔性数组 chars[] 的大小
    ObjString* string = (ObjString*)allocateObject(
        sizeof(ObjString) + length + 1,
        OBJ_STRING
    );
    string->length = length;
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

// 从 C 字符串创建 ObjString (驻留 + 拷贝)
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
   
    // 1. 查重：如果字符串池中已有该字符串，直接返回旧对象的指针
    // 这保证了系统中相同的字符串永远只有一份
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
   
    // 2. 分配：申请一块连续内存
    ObjString* string = allocateString(length);
    if (string == NULL) {
        fprintf(stderr, "Allocation failed for string of length %d.\n", length);
        return NULL;  // Propagate failure
    }
   
    // 3. 拷贝：将数据复制到柔性数组成员 chars 中
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0'; // 补上 C 风格结束符
    string->hash = hash;
   
    // 4. 入池：将新字符串加入驻留表
    if (!tableSet(&vm.strings, string, NIL_VAL)) {
        // Handle table set failure (rare, but possible)
        fprintf(stderr, "Failed to intern string.\n");
        // Cleanup allocated string (optional, but good practice)
        freeObject(&vm, (Obj*)string);
        return NULL;
    }
   
    return string;
}

// === 其他对象创建 ===
ObjNative* newNative(NativeFn function) {
    ObjNative* native = (ObjNative*)allocateObject(sizeof(ObjNative), OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjClip* newClip(ObjString* path) {
    ObjClip* clip = (ObjClip*)allocateObject(sizeof(ObjClip), OBJ_CLIP);
   
    // 初始化所有字段，避免脏数据
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
   
    return clip;
}

// [Add Function]
ObjTimeline* newTimeline(u32 width, u32 height, double fps) {
    ObjTimeline* obj = (ObjTimeline*)allocateObject(sizeof(ObjTimeline), OBJ_TIMELINE);
   
    // 调用引擎 API 创建底层 Timeline
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
            // 打印更多调试信息
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
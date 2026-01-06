// src/vm/object.h

#pragma once
#include "value.h"
// --- Forward Declarations ---
// 前置声明 VM 结构体，解决 vm.h 和 object.h 之间的循环依赖
typedef struct VM VM;
// --- Object Types ---
typedef enum {
    OBJ_STRING,
    OBJ_LIST, // [新增] 列表类型
    OBJ_NATIVE,
    OBJ_CLIP,
    OBJ_TIMELINE,
} ObjType;
// --- Base Object Header ---
struct sObj {
    struct sObj* next; // Offset 0: 放在首位，优化 GC 链表遍历 (Ptr chasing)
    u8 type; // Offset 8: 显式 u8
    bool isMarked; // Offset 9
    // Padding: 6 bytes (Compiler auto-filled to 16 bytes)
};
// --- Native Function ---
typedef Value (*NativeFn)(VM* vm, i32 argCount, Value* args);
typedef struct sObjNative {
    Obj obj;
    NativeFn function;
} ObjNative;
// --- String Object ---
// Layout: [Obj(16)] [Len(4)] [Hash(4)] [Chars...]
struct sObjString {
    Obj obj;
    u32 length;
    u32 hash;
    char chars[];
};
typedef struct {
    Obj obj;
    u32 count;
    u32 capacity;
    Value* items; // 动态数组
} ObjList;
// --- Clip Object ---
// Optimized Layout: Sorted by size to remove all internal padding.
struct sObjClip {
    Obj obj; // 16 bytes
    struct sObjString* path; // 8 bytes
  
    // 8-byte aligned (Doubles) - Grouped together
    double duration;
    double start_time;
    double in_point;
    double out_point;
    double fps;
    double default_scale_x;
    double default_scale_y;
    double default_x;
    double default_y;
    double default_opacity;
  
    // 4-byte aligned (Ints) - Grouped together
    u32 width;
    u32 height;
    i32 layer;
  
    // Padding: 4 bytes at the end (for 8-byte alignment)
};
// Forward Declaration
struct Timeline;
typedef struct sObjTimeline {
    Obj obj;
    struct Timeline* timeline;
} ObjTimeline;
// --- Macros ---
#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_LIST(value) isObjType(value, OBJ_LIST)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_CLIP(value) isObjType(value, OBJ_CLIP)
#define IS_TIMELINE(value) isObjType(value, OBJ_TIMELINE)
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_LIST(value) ((ObjList*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLIP(value) ((ObjClip*)AS_OBJ(value))
#define AS_TIMELINE(value) ((ObjTimeline*)AS_OBJ(value))
// --- Inline Helpers ---
static INLINE bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == (u8)type;
}
// --- API (Context-Aware) ---
// 关键修改：所有对象创建函数现在必须接收 VM* 上下文
ObjString* copyString(VM* vm, const char* chars, i32 length);
ObjString* takeString(VM* vm, char* chars, i32 length);
ObjList* newList(VM* vm);
ObjNative* newNative(VM* vm, NativeFn function);
ObjClip* newClip(VM* vm, ObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
void printObject(Value value);
// src/vm/object.h

#ifndef LUNA_OBJECT_H
#define LUNA_OBJECT_H

#include "common.h"
#include "value.h"

// 1. 定义对象类型枚举
typedef enum {
    OBJ_STRING,
    OBJ_NATIVE,
    OBJ_CLIP,
    // ... 其他类型
} ObjType;

// 2. 🔴 关键！先定义基类结构体 (struct sObj)
// 只有先定义了它，后面的结构体才能包含 "Obj obj;"
struct sObj {
    ObjType type;
    struct sObj* next;
};

// 3. 定义函数指针类型 (用于 Native Function)
typedef Value (*NativeFn)(int argCount, Value* args);

// 4. 定义原生函数对象
typedef struct {
    Obj obj; // 这里使用 obj，必须保证上面的 struct sObj 已经定义
    NativeFn function;
} ObjNative;

// 5. 定义字符串对象
struct sObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

// 6. 定义视频片段对象 (Clip)
typedef struct {
    Obj obj;
    struct sObjString* path;
    
    // === 新增/确认这些字段 ===
    double duration;    // 秒
    double start_time;  // 轨道时间
    int width;          // 视频宽
    int height;         // 视频高
    double fps;         // 帧率
    // ======================
    
    double in_point;
    double out_point;
    int layer;
} ObjClip;

// 7. 宏定义
#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

#define IS_STRING(value)  isObjType(value, OBJ_STRING)
#define IS_NATIVE(value)  isObjType(value, OBJ_NATIVE)
#define IS_CLIP(value)    isObjType(value, OBJ_CLIP)

#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_NATIVE(value)  (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLIP(value)    ((ObjClip*)AS_OBJ(value))

// 内联函数
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// 函数声明
ObjString* copyString(const char* chars, int length);
ObjNative* newNative(NativeFn function);
ObjClip* newClip(ObjString* path); // 记得确保 object.c 里有实现
void printObject(Value value);

#endif
// src/core/object.h

#pragma once
#include "chunk.h"
#include "table.h"
// 前置声明
typedef struct VM VM;
typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjList ObjList;
typedef struct sObjDict ObjDict;
typedef struct sObjFunction ObjFunction;
typedef struct sObjNative ObjNative;
typedef struct sObjClip ObjClip;
typedef struct sObjTimeline ObjTimeline;
typedef struct sObjClass ObjClass;
typedef struct sObjInstance ObjInstance;
typedef struct sObjBoundMethod ObjBoundMethod;
typedef struct sObjClosure ObjClosure;
typedef struct sObjUpvalue ObjUpvalue;
// --- Object Types ---
typedef enum {
    OBJ_STRING,
    OBJ_LIST,
    OBJ_DICT,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLIP,
    OBJ_TIMELINE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_CLOSURE,
    OBJ_UPVALUE
} ObjType;
// --- Base Object Header ---
struct sObj {
    struct sObj* next;
    u8 type; // ObjType
    bool isMarked; // GC Mark Flag
};
// --- String Object ---
struct sObjString {
    Obj obj;
    u32 length;
    u32 hash;
    char chars[]; // Flexible Array Member (C99)
};
// --- List Object ---
struct sObjList {
    Obj obj;
    u32 count;
    u32 capacity;
    Value* items;
};
// --- Dict Object ---
struct sObjDict {
    Obj obj;
    Table items;
};
// --- Function Object (Prototype) ---
struct sObjFunction {
    Obj obj;
    i32 arity;
    i32 minArity; // [新增] 最少需要的参数数量 (Required)
    i32 upvalueCount;
    Chunk chunk;
    ObjString* name;
    ObjString** paramNames; // [新增] 参数名称数组，用于关键字匹配
};
// --- Native Function ---
typedef Value (*NativeFn)(VM* vm, i32 argCount, Value* args);
struct sObjNative {
    Obj obj;
    NativeFn function;
};
// --- Upvalue Object ---
struct sObjUpvalue {
    Obj obj;
    Value* location; // 指向栈上的值 (Open) 或指向 closed 字段 (Closed)
    Value closed;
    struct sObjUpvalue* next; // 用于 Open Upvalues 链表
};
// --- Closure Object ---
struct sObjClosure {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    i32 upvalueCount;
};
// --- Clip Object ---
struct sObjClip {
    Obj obj;
    struct sObjString* path;
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
    u32 width;
    u32 height;
    i32 layer;
};
// --- Timeline Object ---
// Timeline 结构的具体定义在 engine/timeline.h 中
struct Timeline;
struct sObjTimeline {
    Obj obj;
    struct Timeline* timeline;
};
// --- Class & Instance ---
struct sObjClass {
    Obj obj;
    ObjString* name;
    struct sObjClass* superclass;
    Table methods;
};
struct sObjInstance {
    Obj obj;
    ObjClass* klass;
    Table fields;
};
struct sObjBoundMethod {
    Obj obj;
    Value receiver;
    Value method; // 通常是 ObjClosure*
};
// --- Macros ---
#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_LIST(value) isObjType(value, OBJ_LIST)
#define IS_DICT(value) isObjType(value, OBJ_DICT)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_CLIP(value) isObjType(value, OBJ_CLIP)
#define IS_TIMELINE(value) isObjType(value, OBJ_TIMELINE)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_UPVALUE(value) isObjType(value, OBJ_UPVALUE)
// 同质列表检查宏
#define IS_LIST_HOMOGENEOUS(value) (IS_LIST(value) && isListHomogeneous(AS_LIST(value)))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_LIST(value) ((ObjList*)AS_OBJ(value))
#define AS_DICT(value) ((ObjDict*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_UPVALUE(value) ((ObjUpvalue*)AS_OBJ(value))
#define AS_CLIP(value) ((ObjClip*)AS_OBJ(value))
#define AS_TIMELINE(value) ((ObjTimeline*)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
// --- Inline Helpers ---
static INLINE bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
// --- API ---
ObjString* copyString(VM* vm, const char* chars, i32 length);
ObjString* takeString(VM* vm, char* chars, i32 length);
ObjList* newList(VM* vm);
ObjDict* newDict(VM* vm);
ObjFunction* newFunction(VM* vm);
ObjNative* newNative(VM* vm, NativeFn function);
ObjClosure* newClosure(VM* vm, ObjFunction* function);
ObjUpvalue* newUpvalue(VM* vm, Value* slot);
ObjClip* newClip(VM* vm, ObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
ObjClass* newClass(VM* vm, ObjString* name);
ObjInstance* newInstance(VM* vm, ObjClass* klass);
ObjBoundMethod* newBoundMethod(VM* vm, Value receiver, Value method);
void printObject(Value value);
// 类型检查辅助函数
bool typesMatch(Value a, Value b);
bool isListHomogeneous(ObjList* list);

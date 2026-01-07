// src/vm/object.h

#pragma once

#include "table.h"
#include "chunk.h"

typedef struct VM VM;
typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjClip ObjClip;
typedef struct sObjNative ObjNative;
typedef struct sObjTimeline ObjTimeline;
typedef struct sObjClass ObjClass;
typedef struct sObjInstance ObjInstance;
typedef struct sObjBoundMethod ObjBoundMethod;
typedef struct sObjClosure ObjClosure; // [新增]
typedef struct sObjUpvalue ObjUpvalue; // [新增]
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
    OBJ_CLOSURE, // [新增]
    OBJ_UPVALUE // [新增]
} ObjType;
// --- Base Object Header ---
struct sObj {
    struct sObj* next;
    u8 type;
    bool isMarked;
};
// --- Native Function ---
typedef Value (*NativeFn)(VM* vm, i32 argCount, Value* args);
typedef struct sObjNative {
    Obj obj;
    NativeFn function;
} ObjNative;
// --- String Object ---
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
    Value* items;
} ObjList;
typedef struct {
    Obj obj;
    Table items; // 复用 Lox 中的 Table (哈希表)
} ObjDict;
// --- Function Object (Prototype) ---
typedef struct {
    Obj obj;
    i32 arity;
    i32 upvalueCount; // [新增] 该函数需要捕获多少个上值
    Chunk chunk;
    ObjString* name;
} ObjFunction;
// [新增] 上值对象
struct sObjUpvalue {
    Obj obj;
    Value* location; // 指向栈上的值，或者指向 closed
    Value closed; // 变量离开栈后的存储位置
    struct sObjUpvalue* next; // 链表指针
};
// [新增] 闭包对象 (函数的运行时实例)
struct sObjClosure {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues; // 指针数组
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
struct Timeline;
typedef struct sObjTimeline {
    Obj obj;
    struct Timeline* timeline;
} ObjTimeline;
// --- Class & Instance ---
typedef struct sObjClass {
    Obj obj;
    ObjString* name;
    struct sObjClass* superclass;
    Table methods;
} ObjClass;
typedef struct sObjInstance {
    Obj obj;
    ObjClass* klass;
    Table fields;
} ObjInstance;
typedef struct sObjBoundMethod {
    Obj obj;
    Value receiver;
    Value method; // 这里的 method 现在通常是 ObjClosure*
} ObjBoundMethod;
// --- Macros ---
#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_LIST(value) isObjType(value, OBJ_LIST)
#define IS_LIST_HOMOGENEOUS(value) (IS_LIST(value) && isListHomogeneous(AS_LIST(value))) // [增强] 新增宏，用于检查列表同质性
#define IS_DICT(value) isObjType(value, OBJ_DICT)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_CLIP(value) isObjType(value, OBJ_CLIP)
#define IS_TIMELINE(value) isObjType(value, OBJ_TIMELINE)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE) // [新增]
#define IS_UPVALUE(value) isObjType(value, OBJ_UPVALUE) // [新增]
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_LIST(value) ((ObjList*)AS_OBJ(value))
#define AS_DICT(value) ((ObjDict*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_CLIP(value) ((ObjClip*)AS_OBJ(value))
#define AS_TIMELINE(value) ((ObjTimeline*)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value)) // [新增]
// --- Inline Helpers ---
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
// --- API ---
ObjString* copyString(VM* vm, const char* chars, i32 length);
ObjString* takeString(VM* vm, char* chars, i32 length);
ObjList* newList(VM* vm);
ObjDict* newDict(VM* vm); // [新增]
ObjFunction* newFunction(VM* vm);
ObjNative* newNative(VM* vm, NativeFn function);
ObjClip* newClip(VM* vm, ObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
ObjClass* newClass(VM* vm, ObjString* name);
ObjInstance* newInstance(VM* vm, ObjClass* klass);
ObjBoundMethod* newBoundMethod(VM* vm, Value receiver, Value method);
ObjClosure* newClosure(VM* vm, ObjFunction* function); // [新增]
ObjUpvalue* newUpvalue(VM* vm, Value* slot); // [新增]
void printObject(Value value);
bool typesMatch(Value a, Value b);
bool isListHomogeneous(ObjList* list);
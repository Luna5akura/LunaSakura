// src/vm/object.c

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "vm.h"
#include "engine/timeline.h"
// === Allocation Helper ===
static Obj* allocateObject(VM* vm, size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(vm, NULL, 0, size);
    object->type = type;
    object->isMarked = false;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}
// === String ===
static ObjString* allocateString(VM* vm, i32 length) {
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = (u32)length;
    return string;
}
static u32 hashString(const char* key, i32 length) {
    // [调整] 使用UTF-8安全的FNV-1a哈希算法，逐字节处理，支持多字节字符
    u32 hash = 2166136261u;
    for (i32 i = 0; i < length; i++) {
        hash ^= (u8)key[i];  // 逐字节XOR，支持UTF-8
        hash *= 16777619;
    }
    return hash;
}
ObjString* copyString(VM* vm, const char* chars, i32 length) {
    // [调整] 添加UTF-8验证（可选，但为安全起见添加简单检查），但不强制错误，仅哈希调整
    u32 hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) return interned;
    ObjString* string = allocateString(vm, length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;
    push(vm, OBJ_VAL(string));
    tableSet(vm, &vm->strings, OBJ_VAL(string), NIL_VAL);
    pop(vm);
    return string;
}
ObjString* takeString(VM* vm, char* chars, i32 length) {
    u32 hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) {
        (void)reallocate(vm, chars, sizeof(char) * (length + 1), 0);
        return interned;
    }
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = (u32)length;
    string->hash = hash;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    (void)reallocate(vm, chars, sizeof(char) * (length + 1), 0);
    tableSet(vm, &vm->strings, OBJ_VAL(string), NIL_VAL);
    return string;
}
ObjList* newList(VM* vm) {
    ObjList* list = (ObjList*)allocateObject(vm, sizeof(ObjList), OBJ_LIST);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return list;
}
ObjDict* newDict(VM* vm) { // [新增]
    ObjDict* dict = (ObjDict*)allocateObject(vm, sizeof(ObjDict), OBJ_DICT);
    initTable(&dict->items);
    return dict;
}
ObjFunction* newFunction(VM* vm) {
    ObjFunction* function = (ObjFunction*)allocateObject(vm, sizeof(ObjFunction), OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0; // [新增]
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}
ObjNative* newNative(VM* vm, NativeFn function) {
    ObjNative* native = (ObjNative*)allocateObject(vm, sizeof(ObjNative), OBJ_NATIVE);
    native->function = function;
    return native;
}
// [新增] 创建闭包
ObjClosure* newClosure(VM* vm, ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(vm, ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }
    ObjClosure* closure = (ObjClosure*)allocateObject(vm, sizeof(ObjClosure), OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}
// [新增] 创建上值
ObjUpvalue* newUpvalue(VM* vm, Value* slot) {
    ObjUpvalue* upvalue = (ObjUpvalue*)allocateObject(vm, sizeof(ObjUpvalue), OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}
ObjClip* newClip(VM* vm, ObjString* path) {
    ObjClip* clip = (ObjClip*)allocateObject(vm, sizeof(ObjClip), OBJ_CLIP);
    clip->path = path;
    // ... (初始化其他字段为0/默认值)
    clip->duration = 0; clip->start_time = 0; clip->in_point = 0; clip->out_point = 0;
    clip->fps = 0; clip->width = 0; clip->height = 0; clip->layer = 0;
    clip->default_scale_x = 1; clip->default_scale_y = 1;
    clip->default_x = 0; clip->default_y = 0; clip->default_opacity = 1;
    return clip;
}
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps) {
    ObjTimeline* obj = (ObjTimeline*)allocateObject(vm, sizeof(ObjTimeline), OBJ_TIMELINE);
    obj->timeline = timeline_create(vm, width, height, fps);
    return obj;
}
ObjClass* newClass(VM* vm, ObjString* name) {
    ObjClass* klass = (ObjClass*)allocateObject(vm, sizeof(ObjClass), OBJ_CLASS);
    klass->name = name;
    klass->superclass = NULL;
    initTable(&klass->methods);
    return klass;
}
ObjInstance* newInstance(VM* vm, ObjClass* klass) {
    ObjInstance* instance = (ObjInstance*)allocateObject(vm, sizeof(ObjInstance), OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}
ObjBoundMethod* newBoundMethod(VM* vm, Value receiver, Value method) {
    ObjBoundMethod* bound = (ObjBoundMethod*)allocateObject(vm, sizeof(ObjBoundMethod), OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_CLOSURE: // [新增]
            printObject(OBJ_VAL(AS_CLOSURE(value)->function));
            break;
        case OBJ_UPVALUE: // [新增]
            printf("upvalue");
            break;
        case OBJ_DICT: { // [新增]
            printf("<dict>");
            break;
        }
        case OBJ_BOUND_METHOD:
            printObject(AS_BOUND_METHOD(value)->method);
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
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
        case OBJ_FUNCTION: {
            ObjFunction* function = AS_FUNCTION(value);
            if (function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", function->name->chars);
            }
            break;
        }
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLIP:
            if (AS_CLIP(value)->path != NULL) printf("<clip \"%s\">", AS_CLIP(value)->path->chars);
            else printf("<clip>");
            break;
        case OBJ_TIMELINE:
            printf("<timeline>");
            break;
    }
}

// [新增] 检查列表是否同质（所有元素类型相同）
bool isListHomogeneous(ObjList* list) {
    if (list->count == 0) return true; // 空列表视为同质
    // ObjType firstType = VALUE_TYPE(list->items[0]); // 假设Value有VALUE_TYPE宏，如果没有，可用类似isObjType的逻辑
    for (u32 i = 1; i < list->count; i++) {
        if (!typesMatch(list->items[0], list->items[i])) {
            return false;
        }
    }
    return true;
}
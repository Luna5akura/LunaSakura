// src/core/object.c

#include "memory.h"
#include "vm/vm.h"
#include <string.h>

// === Allocation Helper ===
static Obj* allocateObject(VM* vm, size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(vm, NULL, 0, size);
    object->type = type;
    object->isMarked = false;
   
    // 插入到 VM 对象链表头部
    object->next = vm->objects;
    vm->objects = object;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
    return object;
}
// === String ===
static ObjString* allocateString(VM* vm, i32 length) {
    // 额外 +1 用于 null terminator
    ObjString* string = (ObjString*)allocateObject(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = (u32)length;
    return string;
}
// FNV-1a Hash Algorithm
static u32 hashString(const char* key, i32 length) {
    u32 hash = 2166136261u;
    for (i32 i = 0; i < length; i++) {
        // [重要] 转换为 u8 避免符号扩展导致的负数计算错误
        hash ^= (u8)key[i];
        hash *= 16777619;
    }
    return hash;
}

static u32 next_param_lookup_capacity(i32 arity) {
    u32 capacity = 8;
    while (capacity < (u32)(arity * 2)) capacity <<= 1;
    return capacity;
}

static void build_param_lookup_entries(ParamLookupEntry* entries, u32 capacity, ObjString** names, i32 arity) {
    u32 mask = capacity - 1;
    for (i32 i = 0; i < arity; i++) {
        ObjString* key = names[i];
        u32 slot = key->hash & mask;
        while (entries[slot].key != NULL) {
            slot = (slot + 1) & mask;
        }
        entries[slot].key = key;
        entries[slot].index = (u8)i;
    }
}

void buildFunctionParamLookup(VM* vm, ObjFunction* function) {
    if (!function) return;
    if (function->paramLookup) {
        FREE_ARRAY(vm, ParamLookupEntry, function->paramLookup, function->paramLookupCapacity);
        function->paramLookup = NULL;
        function->paramLookupCapacity = 0;
    }
    if (function->arity <= 0 || !function->paramNames) return;

    function->paramLookupCapacity = next_param_lookup_capacity(function->arity);
    function->paramLookup = ALLOCATE(vm, ParamLookupEntry, function->paramLookupCapacity);
    for (u32 i = 0; i < function->paramLookupCapacity; i++) {
        function->paramLookup[i].key = NULL;
        function->paramLookup[i].index = 0;
    }

    build_param_lookup_entries(function->paramLookup, function->paramLookupCapacity, function->paramNames, function->arity);
}

void buildNativeParamLookup(VM* vm, ObjNative* native) {
    if (!native) return;
    if (native->paramLookup) {
        FREE_ARRAY(vm, ParamLookupEntry, native->paramLookup, native->paramLookupCapacity);
        native->paramLookup = NULL;
        native->paramLookupCapacity = 0;
    }
    if (native->arity <= 0 || !native->paramNames) return;

    native->paramLookupCapacity = next_param_lookup_capacity(native->arity);
    native->paramLookup = ALLOCATE(vm, ParamLookupEntry, native->paramLookupCapacity);
    for (u32 i = 0; i < native->paramLookupCapacity; i++) {
        native->paramLookup[i].key = NULL;
        native->paramLookup[i].index = 0;
    }

    build_param_lookup_entries(native->paramLookup, native->paramLookupCapacity, native->paramNames, native->arity);
}
ObjString* copyString(VM* vm, const char* chars, i32 length) {
    u32 hash = hashString(chars, length);
   
    // String Interning: 检查是否已存在
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) return interned;
   
    // 分配新字符串
    ObjString* string = allocateString(vm, length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;
   
    // GC Safety: push 防止 tableSet 触发 GC 回收新创建的 string
    push(vm, OBJ_VAL(string));
    tableSet(vm, &vm->strings, OBJ_VAL(string), NIL_VAL);
    pop(vm);
   
    return string;
}
ObjString* takeString(VM* vm, char* chars, i32 length) {
    u32 hash = hashString(chars, length);
   
    ObjString* interned = tableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL) {
        // 既然已经存在，释放传入的 chars 缓冲区（假设所有权已移交）
        // 使用 reallocate 而不是 free 是为了统一内存管理接口
        // 注意：这要求 chars 必须是用兼容的分配器分配的
        FREE_ARRAY(vm, char, chars, length + 1);
        return interned;
    }
   
    ObjString* string = allocateString(vm, length);
    string->length = (u32)length;
    string->hash = hash;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
   
    // 释放旧缓冲区
    FREE_ARRAY(vm, char, chars, length + 1);
   
    push(vm, OBJ_VAL(string));
    tableSet(vm, &vm->strings, OBJ_VAL(string), NIL_VAL);
    pop(vm);
   
    return string;
}
// === Objects Constructors ===
ObjList* newList(VM* vm) {
    ObjList* list = (ObjList*)allocateObject(vm, sizeof(ObjList), OBJ_LIST);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return list;
}
ObjDict* newDict(VM* vm) {
    ObjDict* dict = (ObjDict*)allocateObject(vm, sizeof(ObjDict), OBJ_DICT);
    initTable(&dict->items);
    return dict;
}
ObjFunction* newFunction(VM* vm) {
    ObjFunction* function = (ObjFunction*)allocateObject(vm, sizeof(ObjFunction), OBJ_FUNCTION);
    function->arity = 0;
    function->minArity = 0; // [新增] 必须初始化
    function->upvalueCount = 0;
    function->name = NULL;
    function->paramNames = NULL; // [重要修复] 必须初始化为 NULL，否则 reallocate 会崩溃
    function->paramCapacity = 0;
    function->paramLookup = NULL;
    function->paramLookupCapacity = 0;
    initChunk(&function->chunk);
    return function;
}
ObjNative* newNative(VM* vm, NativeFn function) {
    ObjNative* native = (ObjNative*)allocateObject(vm, sizeof(ObjNative), OBJ_NATIVE);
    native->function = function;
    native->arity = -1;
    native->minArity = 0;
    native->paramNames = NULL;
    native->paramCapacity = 0;
    native->paramLookup = NULL;
    native->paramLookupCapacity = 0;
    return native;
}

ObjNative* newNativeWithSignature(VM* vm, NativeFn function, i32 arity, i32 minArity, const char* const* paramNames) {
    ObjNative* native = newNative(vm, function);
    if (arity <= 0 || !paramNames) {
        native->arity = arity;
        native->minArity = minArity;
        return native;
    }

    native->arity = arity;
    native->minArity = minArity;
    native->paramCapacity = arity;
    native->paramNames = ALLOCATE(vm, ObjString*, native->paramCapacity);
    for (i32 i = 0; i < arity; i++) {
        native->paramNames[i] = copyString(vm, paramNames[i], (i32)strlen(paramNames[i]));
    }
    buildNativeParamLookup(vm, native);
    return native;
}
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
ObjUpvalue* newUpvalue(VM* vm, Value* slot) {
    ObjUpvalue* upvalue = (ObjUpvalue*)allocateObject(vm, sizeof(ObjUpvalue), OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}
ObjClass* newClass(VM* vm, ObjString* name) {
    ObjClass* klass = (ObjClass*)allocateObject(vm, sizeof(ObjClass), OBJ_CLASS);
    klass->name = name;
    klass->superclass = NULL;
    initTable(&klass->methods);
    klass->cachedMethodName = NULL;
    klass->cachedMethodValue = NIL_VAL;
    klass->cachedMethodValid = false;
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
ObjForeign* newForeign(VM* vm, size_t size, const ForeignClassMethods* methods) {
    // 分配具体业务结构体的大小 (例如 sizeof(ObjClip))
    // 强制转换为 ObjForeign* 是安全的，因为 ObjForeign 是它们的第一个成员
    ObjForeign* object = (ObjForeign*)allocateObject(vm, size, OBJ_FOREIGN);
    object->methods = methods;
    return object;
}
// === Print ===
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
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
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLOSURE:
            printObject(OBJ_VAL(AS_CLOSURE(value)->function));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            Value v = *AS_UPVALUE(value)->location;
            printf("(->"); printValue(v); printf(")");
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            printObject(AS_BOUND_METHOD(value)->method);
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
        case OBJ_DICT: {
            ObjDict* dict = AS_DICT(value);
            printf("{");
            bool first = true;
            for (u32 i = 0; i < dict->items.capacity; i++) {
                if (!IS_NIL(dict->items.entries[i].key)) {
                    if (!first) printf(", ");
                    printValue(dict->items.entries[i].key);
                    printf(": ");
                    printValue(dict->items.entries[i].value);
                    first = false;
                }
            }
            printf("}");
            break;
        }
        case OBJ_FOREIGN: {
            ObjForeign* foreign = AS_FOREIGN(value);
            // 使用注册的类型名，例如 <clip>, <timeline>
            printf("<%s>", foreign->methods->typeName);
            break;
        }
    }
}
// === Type Checking ===
// 辅助：获取值的类型标签 (包括基本类型和对象类型)
// 仅用于 typesMatch 内部
typedef enum {
    TYPE_NIL,
    TYPE_BOOL,
    TYPE_NUMBER,
    TYPE_STRING,
    TYPE_LIST,
    TYPE_DICT,
    TYPE_FUNCTION,
    TYPE_CLASS,
    TYPE_INSTANCE,
    TYPE_OTHER
} HighLevelType;
static HighLevelType getValueType(Value v) {
    if (IS_NIL(v)) return TYPE_NIL;
    if (IS_BOOL(v)) return TYPE_BOOL;
    if (IS_NUMBER(v)) return TYPE_NUMBER;
    if (IS_OBJ(v)) {
        switch (OBJ_TYPE(v)) {
            case OBJ_STRING: return TYPE_STRING;
            case OBJ_LIST: return TYPE_LIST;
            case OBJ_DICT: return TYPE_DICT;
            case OBJ_FUNCTION:
            case OBJ_CLOSURE:
            case OBJ_NATIVE:
            case OBJ_BOUND_METHOD: return TYPE_FUNCTION; // 广义函数
            case OBJ_CLASS: return TYPE_CLASS;
            case OBJ_INSTANCE: return TYPE_INSTANCE;
            default: return TYPE_OTHER;
        }
    }
    return TYPE_OTHER;
}
bool typesMatch(Value a, Value b) {
#ifdef NAN_BOXING
    if (a == b) return true;
#endif
    return getValueType(a) == getValueType(b);
}
bool isListHomogeneous(ObjList* list) {
    if (list->count <= 1) return true;
   
    HighLevelType firstType = getValueType(list->items[0]);
   
    for (u32 i = 1; i < list->count; i++) {
        if (getValueType(list->items[i]) != firstType) {
            return false;
        }
    }
    return true;
}

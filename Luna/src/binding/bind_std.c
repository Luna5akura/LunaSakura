// src/binding/bind_std.c

#include <stdio.h>
#include "vm/vm.h"
#include "vm/memory.h"
// --- Helper: Type Checking ---
// 用于确保列表是同类型的 (Homogeneous)
bool typesMatch(Value a, Value b) {
    if (IS_NUMBER(a) && IS_NUMBER(b)) return true;
    if (IS_BOOL(a) && IS_BOOL(b)) return true;
    if (IS_NIL(a) && IS_NIL(b)) return true;
    if (IS_OBJ(a) && IS_OBJ(b)) {
        return OBJ_TYPE(a) == OBJ_TYPE(b);
    }
    return false;
}
// --- List Native Functions ---
// Constructor: List()
Value nativeList(VM* vm, i32 argCount, Value* args) {
    return OBJ_VAL(newList(vm));
}
// push(list, item)
Value nativePush(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2 || !IS_LIST(args[0])) {
        fprintf(stderr, "Usage: push(list, item)\n");
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[0]);
    Value item = args[1];
    // 同类型检查
    if (list->count > 0) {
        if (!typesMatch(list->items[0], item)) {
            fprintf(stderr, "Runtime Error: List is homogeneous. Cannot mix types.\n");
            return NIL_VAL;
        }
    }
    // 扩容
    if (list->capacity < list->count + 1) {
        u32 oldCapacity = list->capacity;
        list->capacity = GROW_CAPACITY(oldCapacity);
        list->items = GROW_ARRAY(vm, Value, list->items, oldCapacity, list->capacity);
    }
    list->items[list->count++] = item;
    return NIL_VAL;
}
// pop(list)
Value nativePop(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
  
    if (list->count == 0) return NIL_VAL;
    list->count--;
    return list->items[list->count]; // 返回被弹出的值
}
// len(list or dict)
Value nativeLen(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1) return NUMBER_VAL(0);
    if (IS_LIST(args[0])) {
        ObjList* list = AS_LIST(args[0]);
        return NUMBER_VAL((double)list->count);
    } else if (IS_DICT(args[0])) {
        ObjDict* dict = AS_DICT(args[0]);
        return NUMBER_VAL((double)dict->items.count);
    }
    return NUMBER_VAL(0);
}
// get(list, index)
Value nativeGet(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2 || !IS_LIST(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    double idx = AS_NUMBER(args[1]);
    if (idx < 0 || idx >= list->count) {
        fprintf(stderr, "Runtime Error: List index out of bounds.\n");
        return NIL_VAL;
    }
    return list->items[(int)idx];
}
// set(list, index, value)
Value nativeSet(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3 || !IS_LIST(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    double idx = AS_NUMBER(args[1]);
    Value item = args[2];
    if (idx < 0 || idx >= list->count) return NIL_VAL;
  
    if (list->count > 0 && !typesMatch(list->items[0], item)) {
        fprintf(stderr, "Runtime Error: Type mismatch in homogeneous list.\n");
        return NIL_VAL;
    }
    list->items[(int)idx] = item;
    return NIL_VAL;
}
// clear(list or dict)
Value nativeClear(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_LIST(args[0])) {
        ObjList* list = AS_LIST(args[0]);
        list->count = 0;
    } else if (IS_DICT(args[0])) {
        ObjDict* dict = AS_DICT(args[0]);
        freeTable(vm, &dict->items);
        initTable(&dict->items);
    }
    return NIL_VAL;
}
// --- Dict Native Functions ---
// Constructor: Dict()
Value nativeDict(VM* vm, i32 argCount, Value* args) {
    return OBJ_VAL(newDict(vm));
}
// dict_put(dict, key, value)
Value nativeDictPut(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3 || !IS_DICT(args[0])) {
        fprintf(stderr, "Usage: dict_put(dict, key, value)\n");
        return NIL_VAL;
    }
    ObjDict* dict = AS_DICT(args[0]);
    Value key = args[1];
    Value value = args[2];
    tableSet(vm, &dict->items, key, value);
    return NIL_VAL;
}
// dict_get(dict, key)
Value nativeDictGet(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2 || !IS_DICT(args[0])) {
        fprintf(stderr, "Usage: dict_get(dict, key)\n");
        return NIL_VAL;
    }
    ObjDict* dict = AS_DICT(args[0]);
    Value key = args[1];
    Value value;
    if (tableGet(&dict->items, key, &value)) {
        return value;
    }
    return NIL_VAL;
}
// dict_remove(dict, key)
Value nativeDictRemove(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2 || !IS_DICT(args[0])) {
        fprintf(stderr, "Usage: dict_remove(dict, key)\n");
        return NIL_VAL;
    }
    ObjDict* dict = AS_DICT(args[0]);
    Value key = args[1];
    Value value;
    if (tableGet(&dict->items, key, &value)) {
        tableDelete(&dict->items, key);
        return value;
    }
    return NIL_VAL;
}
// dict_has(dict, key)
Value nativeDictHas(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2 || !IS_DICT(args[0])) {
        fprintf(stderr, "Usage: dict_has(dict, key)\n");
        return FALSE_VAL;
    }
    ObjDict* dict = AS_DICT(args[0]);
    Value key = args[1];
    Value value;
    return BOOL_VAL(tableGet(&dict->items, key, &value));
}
// dict_keys(dict)
Value nativeDictKeys(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_DICT(args[0])) {
        fprintf(stderr, "Usage: dict_keys(dict)\n");
        return OBJ_VAL(newList(vm));
    }
    ObjDict* dict = AS_DICT(args[0]);
    ObjList* list = newList(vm);
    list->capacity = dict->items.count;
    list->items = ALLOCATE(vm, Value, list->capacity);
    list->count = 0;
    for (u32 i = 0; i < dict->items.capacity; i++) {
        Entry* entry = &dict->items.entries[i];
        if (!IS_NIL(entry->key)) {
            list->items[list->count++] = entry->key;
        }
    }
    return OBJ_VAL(list);
}

Value nativeDictValues(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_DICT(args[0])) {
        fprintf(stderr, "Usage: dict_values(dict)\n");
        return OBJ_VAL(newList(vm));
    }
    ObjDict* dict = AS_DICT(args[0]);
    ObjList* list = newList(vm);
    list->capacity = dict->items.count;
    list->items = ALLOCATE(vm, Value, list->capacity);
    list->count = 0;
    for (u32 i = 0; i < dict->items.capacity; i++) {
        Entry* entry = &dict->items.entries[i];
        if (!IS_NIL(entry->key)) {
            list->items[list->count++] = entry->value;
        }
    }
    return OBJ_VAL(list);
}
// --- Registration Entry Point ---
void registerStdBindings(VM* vm) {
    defineNative(vm, "List", nativeList);
    defineNative(vm, "push", nativePush);
    defineNative(vm, "pop", nativePop);
    defineNative(vm, "len", nativeLen);
    defineNative(vm, "get", nativeGet);
    defineNative(vm, "set", nativeSet);
    defineNative(vm, "clear", nativeClear);
    // Dict bindings
    defineNative(vm, "Dict", nativeDict);
    defineNative(vm, "dict_put", nativeDictPut);
    defineNative(vm, "dict_get", nativeDictGet);
    defineNative(vm, "dict_remove", nativeDictRemove);
    defineNative(vm, "dict_has", nativeDictHas);
    defineNative(vm, "dict_keys", nativeDictKeys);
    defineNative(vm, "dict_values", nativeDictValues);
}
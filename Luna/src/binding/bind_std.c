// src/binding/bind_std.c
#include <stdio.h>
#include "vm/vm.h"
#include "vm/memory.h"

// --- Helper: Type Checking ---
// 用于确保列表是同类型的 (Homogeneous)
static bool typesMatch(Value a, Value b) {
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

// len(list)
Value nativeLen(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_LIST(args[0])) return NUMBER_VAL(0);
    ObjList* list = AS_LIST(args[0]);
    return NUMBER_VAL((double)list->count);
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

// clear(list)
Value nativeClear(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    ObjList* list = AS_LIST(args[0]);
    list->count = 0;
    return NIL_VAL;
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
}
// src/vm/memory.c

#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "compiler.h"
#include "engine/timeline.h"

void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    if (vm != NULL) {
        if (newSize > oldSize) vm->bytesAllocated += (newSize - oldSize);
        else vm->bytesAllocated -= (oldSize - newSize);

        if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
            collectGarbage(vm);
#else
            if (vm->bytesAllocated > vm->nextGC) {
                collectGarbage(vm);
            }
#endif
        }
    }
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

void freeObject(VM* vm, Obj* object) {
    switch (object->type) {
        case OBJ_CLOSURE: { // [新增]
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            (void)reallocate(vm, object, sizeof(ObjClosure), 0);
            break;
        }
        case OBJ_UPVALUE: { // [新增]
            (void)reallocate(vm, object, sizeof(ObjUpvalue), 0);
            break;
        }
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            (void)reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(vm, &function->chunk);
            (void)reallocate(vm, object, sizeof(ObjFunction), 0);
            break;
        }
        case OBJ_NATIVE: {
            (void)reallocate(vm, object, sizeof(ObjNative), 0);
            break;
        }
        case OBJ_CLIP: {
            (void)reallocate(vm, object, sizeof(ObjClip), 0);
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* obj = (ObjTimeline*)object;
            if (obj->timeline) {
                timeline_free(vm, obj->timeline);
                obj->timeline = NULL;
            }
            (void)reallocate(vm, object, sizeof(ObjTimeline), 0);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            FREE_ARRAY(vm, Value, list->items, list->capacity);
            (void)reallocate(vm, object, sizeof(ObjList), 0);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(vm, &klass->methods);
            (void)reallocate(vm, object, sizeof(ObjClass), 0);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(vm, &instance->fields);
            (void)reallocate(vm, object, sizeof(ObjInstance), 0);
            break;
        }
        case OBJ_BOUND_METHOD: {
            (void)reallocate(vm, object, sizeof(ObjBoundMethod), 0);
            break;
        }
    }
}

void freeObjects(VM* vm) {
    Obj* object = vm->objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(vm, object);
        object = next;
    }
    vm->objects = NULL;
    free(vm->grayStack);
    vm->grayStack = NULL;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
}

void markObject(VM* vm, Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;
    object->isMarked = true;
    if (vm->grayCapacity < vm->grayCount + 1) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        vm->grayStack = (Obj**)realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
        if (vm->grayStack == NULL) exit(1);
    }
    vm->grayStack[vm->grayCount++] = object;
}

void markValue(VM* vm, Value value) {
    if (IS_OBJ(value)) markObject(vm, AS_OBJ(value));
}

static void markArray(VM* vm, ValueArray* array) {
    for (u32 i = 0; i < array->count; i++) {
        markValue(vm, array->values[i]);
    }
}

static void blackenObject(VM* vm, Obj* object) {
    switch (object->type) {
        case OBJ_CLOSURE: { // [新增]
            ObjClosure* closure = (ObjClosure*)object;
            markObject(vm, (Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject(vm, (Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_UPVALUE: { // [新增]
            markValue(vm, ((ObjUpvalue*)object)->closed);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(vm, bound->receiver);
            markValue(vm, bound->method);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject(vm, (Obj*)klass->name);
            markTable(vm, &klass->methods);
            markObject(vm, (Obj*)klass->superclass);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            markObject(vm, (Obj*)instance->klass);
            markTable(vm, &instance->fields);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            if (function->name) markObject(vm, (Obj*)function->name);
            markArray(vm, &function->chunk.constants);
            break;
        }
        case OBJ_CLIP: {
            ObjClip* clip = (ObjClip*)object;
            if (clip->path) markObject(vm, (Obj*)clip->path);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            markArray(vm, (ValueArray*)list);
            break;
        }
        default: break;
    }
}

static void markRoots(VM* vm) {
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(vm, *slot);
    }
    // [修改] 标记 Closure
    for (i32 i = 0; i < vm->frameCount; i++) {
        markObject(vm, (Obj*)vm->frames[i].closure);
    }
    // [新增] 标记 Open Upvalues
    for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject(vm, (Obj*)upvalue);
    }

    markTable(vm, &vm->globals);
    if (vm->active_timeline) timeline_mark(vm, vm->active_timeline);
    markObject(vm, (Obj*)vm->initString);
    markCompilerRoots(vm);
}

static void traceReferences(VM* vm) {
    while (vm->grayCount > 0) {
        Obj* object = vm->grayStack[--vm->grayCount];
        blackenObject(vm, object);
    }
}

static void sweep(VM* vm) {
    Obj* previous = NULL;
    Obj* object = vm->objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) previous->next = object;
            else vm->objects = object;
            freeObject(vm, unreached);
        }
    }
}

void collectGarbage(VM* vm) {
    markRoots(vm);
    traceReferences(vm);
    tableRemoveWhite(&vm->strings);
    sweep(vm);
    vm->nextGC = vm->bytesAllocated * 2;
}
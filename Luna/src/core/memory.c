// src/core/memory.c

#include <stdlib.h>
#include "memory.h"
#include "vm/vm.h"
#include "compiler/compiler.h"

#define MARK_OBJ(o) markObject(vm, (Obj*)(o))
#define MARK_VAL(v) markValue(vm, (v))

// --- Core Allocation ---
void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    if (vm != NULL) {
        vm->bytesAllocated = vm->bytesAllocated + newSize - oldSize;
        if (newSize > oldSize) {
            size_t threshold = vm->nextGC + (vm->nextGC >> 2); // Hysteresis: +25% 缓冲避免频繁GC
#ifdef DEBUG_STRESS_GC
            collectGarbage(vm);
#else
            if (UNLIKELY(vm->bytesAllocated > threshold)) {
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
    if (UNLIKELY(result == NULL)) {
        if (vm != NULL) {
             collectGarbage(vm);
             result = realloc(pointer, newSize);
             if (result != NULL) return result;
        }
        fprintf(stderr, "Fatal: Out of memory.\n");
        exit(1);
    }
    return result;
}
// --- Object Freer ---
static inline void freeBody(VM* vm, Obj* object) {
    // Switch 顺序调整：高频在前
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            FREE_ARRAY(vm, Value, list->items, list->capacity);
            FREE(vm, ObjList, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(vm, &instance->fields);
            FREE(vm, ObjInstance, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(vm, ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(vm, &function->chunk);
            if (function->paramNames) {
                FREE_ARRAY(vm, ObjString*, function->paramNames, function->arity);
            }
            FREE(vm, ObjFunction, object);
            break;
        }
        case OBJ_DICT: {
            ObjDict* dict = (ObjDict*)object;
            freeTable(vm, &dict->items);
            FREE(vm, ObjDict, object);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(vm, &klass->methods);
            FREE(vm, ObjClass, object);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(vm, ObjBoundMethod, object);
            break;
        case OBJ_UPVALUE:
            FREE(vm, ObjUpvalue, object);
            break;
        case OBJ_TIMELINE: {
            ObjTimeline* obj = (ObjTimeline*)object;
            if (obj->timeline) {
                timeline_free(vm, obj->timeline);
            }
            FREE(vm, ObjTimeline, object);
            break;
        }
        case OBJ_CLIP:
            FREE(vm, ObjClip, object);
            break;
        case OBJ_NATIVE:
            FREE(vm, ObjNative, object);
            break;
    }
}
// 运行时 GC 使用的释放函数
static void freeObject(VM* vm, Obj* object) {
    freeBody(vm, object);
}
void freeObjects(VM* vm) {
    Obj* object = vm->objects;
   
    while (object != NULL) {
        Obj* next = object->next;
       
        freeBody(vm, object);
        object = next;
    }
  
    if (vm->grayStack) {
        free(vm->grayStack);
        vm->grayStack = NULL;
    }
   
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->objects = NULL; // 防止悬垂指针
    vm->bytesAllocated = 0;
}
// --- Garbage Collector (with Incremental Marking) ---
// 真正的标记逻辑 (Slow Path)
void markObjectDo(VM* vm, Obj* object) {
    object->isMarked = true;
    if (UNLIKELY(vm->grayCapacity < vm->grayCount + 1)) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        vm->grayStack = (Obj**)realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
        if (vm->grayStack == NULL) {
            fprintf(stderr, "Fatal: Out of memory during GC.\n");
            exit(1);
        }
    }
    vm->grayStack[vm->grayCount++] = object;
}
static void markArray(VM* vm, ValueArray* array) {
    for (u32 i = 0; i < array->count; i++) {
        markValue(vm, array->values[i]);
    }
}
static void blackenObject(VM* vm, Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    switch (object->type) {
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            MARK_OBJ(instance->klass);
            markTable(vm, &instance->fields);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            Value* items = list->items;
            u32 count = list->count;
            for (u32 i = 0; i < count; i++) {
                MARK_VAL(items[i]);
            }
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            MARK_OBJ(closure->function);
            ObjUpvalue** upvalues = closure->upvalues;
            int count = closure->upvalueCount;
            for (int i = 0; i < count; i++) {
                MARK_OBJ(upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            if (function->name) MARK_OBJ(function->name);
            markArray(vm, &function->chunk.constants);
            if (function->paramNames) {
                ObjString** names = function->paramNames;
                for (i32 i = 0; i < function->arity; i++) {
                    MARK_OBJ(names[i]);
                }
            }
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            MARK_OBJ(klass->name);
            markTable(vm, &klass->methods);
            if (klass->superclass) MARK_OBJ(klass->superclass);
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* objTl = (ObjTimeline*)object;
            Timeline* tl = objTl->timeline;
            if (tl == NULL) break;
            for (u32 i = 0; i < tl->track_count; i++) {
                Track* track = &tl->tracks[i];
                for (u32 j = 0; j < track->clip_count; j++) {
                    TimelineClip* clip = &track->clips[j];
                    if (clip->media) {
                        MARK_OBJ(clip->media);
                    }
                }
            }
            break;
        }
        case OBJ_DICT: {
            ObjDict* dict = (ObjDict*)object;
            markTable(vm, &dict->items);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            MARK_VAL(bound->receiver);
            MARK_VAL(bound->method);
            break;
        }
        case OBJ_UPVALUE: {
            MARK_VAL(((ObjUpvalue*)object)->closed);
            break;
        }
        case OBJ_CLIP: {
            ObjClip* clip = (ObjClip*)object;
            if (clip->path) MARK_OBJ(clip->path);
            break;
        }
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}
static void markRoots(VM* vm) {
    Value* top = vm->stackTop;
    Value* slot = vm->stack;
    while (slot + 4 <= top) {
        if (IS_OBJ(slot[0])) markObject(vm, AS_OBJ(slot[0]));
        if (IS_OBJ(slot[1])) markObject(vm, AS_OBJ(slot[1]));
        if (IS_OBJ(slot[2])) markObject(vm, AS_OBJ(slot[2]));
        if (IS_OBJ(slot[3])) markObject(vm, AS_OBJ(slot[3]));
        slot += 4;
    }
    while (slot < top) {
        if (IS_OBJ(*slot)) markObject(vm, AS_OBJ(*slot));
        slot++;
    }
    i32 frameCount = vm->frameCount;
    CallFrame* frames = vm->frames;
    for (i32 i = 0; i < frameCount; i++) {
        markObject(vm, (Obj*)frames[i].closure);
    }
    for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; ) {
        ObjUpvalue* next = upvalue->next;
#if defined(__GNUC__) || defined(__clang__)
        if (next) __builtin_prefetch(next->next, 0, 1);
#endif
        markObject(vm, (Obj*)upvalue);
        upvalue = next;
    }
    markTable(vm, &vm->globals);
    if (vm->active_timeline) timeline_mark(vm, vm->active_timeline);
    if (vm->initString) markObject(vm, (Obj*)vm->initString);
    markCompilerRoots(vm);
}
#if defined(__GNUC__) || defined(__clang__)
    #define PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#else
    #define PREFETCH(addr)
#endif
static inline void blackenObjectInline(VM* vm, Obj* object) {
    blackenObject(vm, object);
}
// 增量标记：每次处理固定片段（e.g., 100个灰对象）
#define INCREMENTAL_SLICE 100
static void traceReferencesIncremental(VM* vm) {
    Obj** stack = vm->grayStack;
    int remaining = vm->grayCount;
    while (remaining > 0) {
        int slice = remaining > INCREMENTAL_SLICE ? INCREMENTAL_SLICE : remaining;
        for (int i = 0; i < slice; i++) {
            Obj* object = stack[--vm->grayCount];
            blackenObjectInline(vm, object);
        }
        remaining -= slice;
        // 可在此 yield 或检查时间，若需全增量GC
    }
}
static void traceReferences(VM* vm) {
    traceReferencesIncremental(vm); // 使用增量版本
}
static void sweep(VM* vm) {
    Obj** pointer_to_next = &vm->objects;
    Obj* current = vm->objects;
   
    Obj* trash_head = NULL;
    while (current != NULL) {
        Obj* next = current->next;
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(next, 0, 1);
#endif
        if (current->isMarked) {
            current->isMarked = false;
            *pointer_to_next = current;
            pointer_to_next = &current->next;
        } else {
            current->next = trash_head;
            trash_head = current;
        }
        current = next;
    }
    *pointer_to_next = NULL;
    while (trash_head != NULL) {
        Obj* next = trash_head->next;
        freeObject(vm, trash_head);
        trash_head = next;
    }
}
#define MIN_STR_TABLE_CAPACITY 1024
void collectGarbage(VM* vm) {
#ifdef DEBUG_LOG_GC
    size_t before = vm->bytesAllocated;
    printf("-- GC begin: %zu bytes allocated\n", before);
#endif
    markRoots(vm);
    traceReferences(vm);
    tableRemoveWhite(&vm->strings);
    sweep(vm);
    size_t after = vm->bytesAllocated;
    const size_t MIN_HEAP_SIZE = 1024 * 1024;
    const size_t LARGE_HEAP_THRESHOLD = 64 * 1024 * 1024;
   
    if (after < MIN_HEAP_SIZE) {
        vm->nextGC = MIN_HEAP_SIZE;
    } else {
        size_t next_target;
        if (after > LARGE_HEAP_THRESHOLD) {
            size_t growth = after >> 2;
            if (growth > 16 * 1024 * 1024) growth = 16 * 1024 * 1024;
            next_target = after + growth;
        } else {
            next_target = after * 2;
        }
       
        vm->nextGC = next_target;
    }
#ifdef DEBUG_LOG_GC
    printf("-- GC end: %zu bytes allocated (freed %zu)\n", after, before - after);
    printf("-- Next GC threshold: %zu\n", vm->nextGC);
#endif
}
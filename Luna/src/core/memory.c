// src/core/memory.c

#include <stdlib.h>
#include "memory.h"
#include "vm/vm.h"
#include "compiler/compiler.h"

#define MARK_OBJ(o) markObject(vm, (Obj*)(o))
#define MARK_VAL(v) markValue(vm, (v))

// --- Core Allocation ---
void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    // 1. 统计内存并触发 GC
    if (vm != NULL) {
        // 使用有符号运算前先转换，防止 size_t 下溢风险 (虽然逻辑上不太可能)
        // if (newSize > oldSize) {
        //     vm->bytesAllocated += (newSize - oldSize);
        // } else {
        //     vm->bytesAllocated -= (oldSize - newSize);
        // }

        vm->bytesAllocated = vm->bytesAllocated + newSize - oldSize;

        if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
            collectGarbage(vm);
#else
            if (UNLIKELY(vm->bytesAllocated > vm->nextGC)) {
                collectGarbage(vm);
            }
#endif
        }
    }
    // 2. 释放逻辑
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    // 3. 分配/重分配逻辑
    void* result = realloc(pointer, newSize);
   
    // 内存耗尽处理
    if (UNLIKELY(result == NULL)) {
        // 尝试最后一次 GC 挽救
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
            // 运行时 GC 需要计算大小以更新 bytesAllocated
            ObjString* string = (ObjString*)object;
            reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            // 释放数组内容
            FREE_ARRAY(vm, Value, list->items, list->capacity);
            // 释放对象头
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
            // 检查 timeline 是否为 NULL 很重要
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

// [极致优化] VM 销毁时的批量释放
void freeObjects(VM* vm) {
    Obj* object = vm->objects;
    
    // 1. 快速遍历链表
    while (object != NULL) {
        Obj* next = object->next;
        
        // [优化核心] 这里如果不复用 freeObject，而是手写逻辑
        // 我们可以跳过 reallocate 中的 bytesAllocated -= size 步骤
        // 直接调用系统 free()，节省大量的数学运算和内存读取。
        // 但为了代码可维护性，如果你的 reallocate 内部没有复杂的锁或检查，
        // 直接调用 freeBody 也是可以接受的。
        
        // 如果追求极致性能，并且你的底层是 malloc/free：
        /*
        if (object->type == OBJ_STRING) {
             // 甚至不需要读取 string->length，直接 free
             free(object); 
        } else {
             freeBody(vm, object); // 其他类型有子资源，必须走标准流程
        }
        */
        
        freeBody(vm, object);
        object = next;
    }
   
    // 2. 释放 GC 辅助结构
    // 检查指针是否为 NULL 再释放是一个好习惯，虽然 free(NULL) 是安全的
    if (vm->grayStack) {
        free(vm->grayStack);
        vm->grayStack = NULL;
    }
    
    // 3. 清理状态
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->objects = NULL; // 防止悬垂指针
    
    // VM 销毁时，这个归零其实没有实际意义，除非 VM 结构体会被复用
    vm->bytesAllocated = 0; 
}
// --- Garbage Collector ---
// 真正的标记逻辑 (Slow Path)
void markObjectDo(VM* vm, Obj* object) {
    object->isMarked = true;
   
    if (UNLIKELY(vm->grayCapacity < vm->grayCount + 1)) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        // 注意：这里使用系统 realloc 这是一个独立的缓冲区，不计入 VM 托管的 bytesAllocated
        // 或者也可以使用 reallocate(vm, ...) 纳入管理，取决于设计策略。
        // 为了避免 GC 过程中触发 GC (递归灾难)，通常使用 raw realloc 或确保 reallocate 能处理 recursion。
        // 这里使用 raw realloc 安全。
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
// 调试日志宏最好包裹整个块，避免 release 模式下的判断开销
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    // [优化点 1] 使用指针访问 type，编译器更容易优化
    switch (object->type) {
        // [优化点 2] 将高频对象类型放在 Switch 前部 (假设 Instance 和 List 最多)
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            MARK_OBJ(instance->klass);
            markTable(vm, &instance->fields);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            // [优化点 3] 局部变量缓存指针，避免多次间接寻址
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
            // [优化点 4] 循环展开 (如果 upvalue 数量通常很少)
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
            
            // 你的修复是正确的，参数名必须标记
            if (function->paramNames) {
                ObjString** names = function->paramNames;
                // 使用 i32 匹配结构体定义
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
        // [重要修复] Timeline 的遍历
        case OBJ_TIMELINE: {
            ObjTimeline* objTl = (ObjTimeline*)object;
            Timeline* tl = objTl->timeline;
            if (tl == NULL) break; // 防御性编程

            // Timeline -> Track[]
            // 优化：Track 是连续数组，缓存友好
            for (u32 i = 0; i < tl->track_count; i++) {
                Track* track = &tl->tracks[i];
                // Track -> TimelineClip[]
                // 优化：Clips 是连续结构体数组，极佳的缓存局部性
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
        // 字符串和 Native 函数没有引用的子对象，直接跳过
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void markRoots(VM* vm) {
    // --- 1. Stack Optimization (最核心优化) ---
    // [优化 A] 将 stackTop 缓存到局部寄存器变量，避免每次循环都解引用 vm 指针
    Value* top = vm->stackTop;
    Value* slot = vm->stack;

    // [优化 B] 循环展开 (Loop Unrolling) + 手动内联检查
    // 每次处理 4 个槽位，减少循环控制指令 (cmp, jmp) 的开销
    // 现代 CPU 的流水线可以并行处理这些无依赖的检查
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

    // --- 2. Call Frames ---
    // [优化 C] 既然 CallFrame 是数组，同样可以使用局部变量缓存
    i32 frameCount = vm->frameCount;
    CallFrame* frames = vm->frames;
    for (i32 i = 0; i < frameCount; i++) {
        // 注意：CallFrame 里的 closure 一定是对象，不需要 check IS_OBJ
        markObject(vm, (Obj*)frames[i].closure);
    }

    // --- 3. Open Upvalues ---
    // [优化 D] 链表预取 (Prefetching)
    // 链表遍历最大的痛点是等待内存加载。
    // __builtin_prefetch 是 GCC/Clang 的扩展，提示 CPU 提前加载下一个节点到缓存
    for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; ) {
        ObjUpvalue* next = upvalue->next; // 提前读取 next
        
        #if defined(__GNUC__) || defined(__clang__)
        // 提前预取下下个节点，掩盖当前节点的处理延迟
        if (next) __builtin_prefetch(next->next, 0, 1); 
        #endif

        markObject(vm, (Obj*)upvalue);
        upvalue = next;
    }

    // --- 4. Globals ---
    markTable(vm, &vm->globals);

    // --- 5. Special Roots ---
    if (vm->active_timeline) timeline_mark(vm, vm->active_timeline);
    if (vm->initString) markObject(vm, (Obj*)vm->initString);

    // --- 6. Compiler Roots ---
    markCompilerRoots(vm);
}

// 跨平台预取宏
#if defined(__GNUC__) || defined(__clang__)
    // 0: read access, 3: high temporal locality (keep in cache)
    #define PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#else
    #define PREFETCH(addr)
#endif

static inline void blackenObjectInline(VM* vm, Obj* object) {
    blackenObject(vm, object); 
}

static void traceReferences(VM* vm) {
    Obj** stack = vm->grayStack;
    
    // 2. 循环展开 (Loop Unrolling) - 4路并行
    while (vm->grayCount >= 4) {
        // [关键步骤 A]：一次性读取栈顶的 4 个指针
        // 注意：grayCount 指向下一个空位，所以要 -1, -2...
        // 这里为了减少 vm->grayCount 的写操作，我们先读出来
        int top = vm->grayCount;
        Obj* o1 = stack[top - 1];
        Obj* o2 = stack[top - 2];
        Obj* o3 = stack[top - 3];
        Obj* o4 = stack[top - 4];

        // [关键步骤 B]：软件预取 (Software Prefetching)
        // 在我们处理 o1 之前，告诉 CPU："嘿，把 o2, o3, o4 的内存也顺便拉到缓存里来"
        PREFETCH(o1);
        PREFETCH(o2);
        PREFETCH(o3);
        PREFETCH(o4);

        vm->grayCount -= 4;

        // [关键步骤 D]：执行标记
        // 由于预取指令的存在，当处理 o1 时，o2/o3/o4 的数据正在从 RAM 飞向 L1 Cache。
        // 当处理到 o2 时，它可能已经准备好了，从而消除了 CPU 停顿。
        blackenObjectInline(vm, o1);
        blackenObjectInline(vm, o2);
        blackenObjectInline(vm, o3);
        blackenObjectInline(vm, o4);
    }

    while (vm->grayCount > 0) {
        Obj* object = stack[--vm->grayCount];
        blackenObjectInline(vm, object);
    }
}

static void sweep(VM* vm) {
    // [优化 1] 使用二级指针追踪“上一个节点的 next 字段”的地址
    // 这样消除了 if (previous == NULL) 的特殊分支判断
    Obj** pointer_to_next = &vm->objects;
    Obj* current = vm->objects;
    
    // [优化 2] 建立一个临时链表头，用于收集垃圾对象
    // 这将 freeObject 的调用移出了主循环
    Obj* trash_head = NULL;

    while (current != NULL) {
        // [优化 3] 立即缓存下一个节点，防止修改 current->next 后丢失链表
        Obj* next = current->next;
        
        // [优化 4] 软件预取：提前告诉 CPU 加载下下个节点
        // 链表遍历主要瓶颈是内存延迟，预取能大幅提升吞吐量
        #if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(next, 0, 1); 
        #endif

        if (current->isMarked) {
            // --- 存活对象 ---
            current->isMarked = false; // 重置标记
            
            // 将当前对象链接到幸存者链表中
            *pointer_to_next = current;
            
            // 更新追踪指针，指向当前对象的 next 字段
            pointer_to_next = &current->next;
        } else {
            // --- 死亡对象 ---
            // 仅仅是将对象移动到垃圾链表 (极快的指针操作)
            // 不在这里调用 freeObject，保持 CPU 流水线处理这一段循环极其顺畅
            current->next = trash_head;
            trash_head = current;
        }

        current = next;
    }

    // 此时 pointer_to_next 指向最后一个存活对象的 next 字段
    // 必须将其置空，终止存活链表
    *pointer_to_next = NULL;

    // --- 批量释放阶段 ---
    // 此时不再关心全局链表维护，只管无脑释放
    // 这对 CPU 的指令预测非常友好
    while (trash_head != NULL) {
        Obj* next = trash_head->next;
        freeObject(vm, trash_head);
        trash_head = next;
    }
}

// 辅助宏：调整哈希表大小的阈值
#define MIN_STR_TABLE_CAPACITY 1024
#define STR_TABLE_SHRINK_LOAD 0.25 // 负载率低于 25% 则收缩

void collectGarbage(VM* vm) {
#ifdef DEBUG_LOG_GC
    size_t before = vm->bytesAllocated;
    printf("-- GC begin: %zu bytes allocated\n", before);
#endif

    // 1. Mark Roots (Stack, Globals, Upvalues)
    markRoots(vm);

    // 2. Trace References (Gray -> Black)
    traceReferences(vm);

    // 3. String Interning Cleanup
    // 这里是优化的关键点 A
    tableRemoveWhite(&vm->strings);
    
    // // [优化 A] 检查字符串表是否需要收缩
    // // 如果表中元素极少但容量巨大，遍历它会严重拖累 GC 速度
    // if (vm->strings.capacity > MIN_STR_TABLE_CAPACITY &&
    //     vm->strings.count < vm->strings.capacity * STR_TABLE_SHRINK_LOAD) {
    //     // 收缩为当前数量的 2 倍 (保持适当的空间避免立即扩张)
    //     tableAdjustCapacity(vm, &vm->strings, vm->strings.count << 1);
    // }

    // 4. Sweep (Free dead objects)
    sweep(vm);

    size_t after = vm->bytesAllocated;

    // [优化 B] 启发式策略优化：全整数运算 & 大堆保护
    
    // 基础阈值设定 (1MB)
    const size_t MIN_HEAP_SIZE = 1024 * 1024;
    // 大堆阈值 (比如 64MB)，超过这个值后不再翻倍，而是线性增加
    const size_t LARGE_HEAP_THRESHOLD = 64 * 1024 * 1024; 
    
    if (after < MIN_HEAP_SIZE) {
        // 小堆模式：直接设为最小值，减少微小 GC 的频率
        vm->nextGC = MIN_HEAP_SIZE;
    } else {
        // [优化 C] 计算增长因子 (Growth Factor)
        // 使用整数乘法代替浮点除法： (after * 100) / nextGC > 70
        // 注意：这里需要拿 GC *前* 的 nextGC 比较，但此时我们想根据 *当前* 存活量计算下一次
        
        size_t next_target;
        
        // 策略 1: 内存压力检测
        // 如果 GC 后存活对象依然很多（说明内存碎片化低，或者程序确实需要这么多内存）
        // 我们应该稍微激进一点扩容，避免马上又触发 GC
        
        // 策略 2: 防止大堆爆炸
        if (after > LARGE_HEAP_THRESHOLD) {
            // 大堆模式：线性增长 (例如每次增加 32MB 或 当前的 1.2倍)
            // 这里选择 conservative 的策略：+33% 或者 +16MB 取大值
            size_t growth = after >> 2; 
            if (growth > 16 * 1024 * 1024) growth = 16 * 1024 * 1024;
            next_target = after + growth;
        } else {
            // 标准模式：根据之前的压力决定倍率
            // 如果上次 GC 阈值设定得太低，导致本次 GC 时内存几乎满了 (>85%)
            // 那么我们这次就多给点空间 (x2.0)，否则给少点 (x1.5) 保持紧凑
            
            // 注意：我们需要 vm->nextGC 的旧值来判断压力，但它在函数开始时没被修改
            // 这是一个简单的整数压力判断: after * 100 > vm->nextGC * 85
            bool high_pressure = (after * 128) > (vm->nextGC * 64);
            
            if (high_pressure) {
                next_target = after * 2;
            } else {
                // x1.5 倍 (整数写法: after + after / 2)
                next_target = after + (after >> 1);
            }
        }
        
        vm->nextGC = next_target;
    }

#ifdef DEBUG_LOG_GC
    printf("-- GC end: %zu bytes allocated (freed %zu)\n", after, before - after);
    printf("-- Next GC threshold: %zu\n", vm->nextGC);
#endif
}
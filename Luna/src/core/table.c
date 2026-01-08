// src/core/table.c

#include <stdlib.h>

#include "memory.h"
#include "vm/vm.h"

// 负载因子：0.75 是时间/空间的经典平衡点
#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(VM* vm, Table* table) {
    FREE_ARRAY(vm, Entry, table->entries, table->capacity);
    initTable(table);
}

// --- Internal Helper: Find Entry ---
// 核心热路径：查找 Key 对应的 Entry，或者适合插入的空位/墓碑
static INLINE Entry* findEntry(Entry* entries, u32 capacity, Value key) {
    // 假设 capacity 总是 2 的幂
    u32 index = valueHash(key) & (capacity - 1);
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];

        if (IS_NIL(entry->key)) {
            if (IS_NIL(entry->value)) {
                // 找到真正的空位 (Empty)
                // 如果之前遇到过墓碑，返回墓碑以便重用；否则返回这个空位
                return tombstone != NULL ? tombstone : entry;
            } else {
                // 找到墓碑 (Tombstone)，记录下来以便重用
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            // 找到匹配的 Key
            return entry;
        }

        // 线性探测 (Linear Probing)
        index = (index + 1) & (capacity - 1);
    }
}

// --- Internal Helper: Resize ---
static void adjustCapacity(VM* vm, Table* table, u32 capacity) {
    Entry* entries = ALLOCATE(vm, Entry, capacity);
    
    // 初始化新数组
    for (u32 i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    // 重新计算 count (只计算活跃条目，丢弃墓碑)
    table->count = 0;

    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        
        // 跳过空位和墓碑 (墓碑不会被复制，从而被清理)
        if (IS_NIL(entry->key)) continue;

        // [优化] Fast Rehash
        // 在新数组中，我们确定：
        // 1. 没有重复 Key (不需要 valuesEqual)
        // 2. 没有墓碑 (不需要检查 tombstone)
        // 3. 数组还没满
        u32 index = valueHash(entry->key) & (capacity - 1);
        while (!IS_NIL(entries[index].key)) {
            index = (index + 1) & (capacity - 1);
        }

        entries[index].key = entry->key;
        entries[index].value = entry->value;
        table->count++;
    }

    FREE_ARRAY(vm, Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

// --- Operations ---

bool tableGet(Table* table, Value key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;

    *value = entry->value;
    return true;
}

bool tableSet(VM* vm, Table* table, Value key, Value value) {
    // 扩容检查：确保有足够的空位 (包含墓碑占用的位置)
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        u32 capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(vm, table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    
    bool isNewKey = IS_NIL(entry->key);
    // 只有当写入真正的空位时，count 才增加
    // 如果是覆盖墓碑 (isNewKey && !IS_NIL(value))，count 不变，因为墓碑已经计入 load factor
    if (isNewKey && IS_NIL(entry->value)) {
        table->count++;
    }

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, Value key) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;

    // 放置墓碑 (Tombstone)
    entry->key = NIL_VAL;
    entry->value = BOOL_VAL(true); // Tombstone 标记
    // 注意：删除不减少 table->count，防止负载因子计算错误导致无限循环
    
    return true;
}

void tableAddAll(VM* vm, Table* from, Table* to) {
    for (u32 i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (!IS_NIL(entry->key)) {
            tableSet(vm, to, entry->key, entry->value);
        }
    }
}

// --- String Interning ---

ObjString* tableFindString(Table* table, const char* chars, u32 length, u32 hash) {
    if (table->count == 0) return NULL;

    u32 index = hash & (table->capacity - 1);

    for (;;) {
        Entry* entry = &table->entries[index];

        if (IS_NIL(entry->key)) {
            // 如果遇到真正的空位 (非墓碑)，说明字符串不存在
            if (IS_NIL(entry->value)) return NULL;
            // 如果是墓碑，继续探测
        } else if (IS_OBJ(entry->key) && OBJ_TYPE(entry->key) == OBJ_STRING) {
            // [优化] 比较顺序：Hash -> Length -> memcmp
            ObjString* string = AS_STRING(entry->key);
            if (string->hash == hash && 
                string->length == length &&
                memcmp(string->chars, chars, length) == 0) {
                return string;
            }
        }

        index = (index + 1) & (table->capacity - 1);
    }
}

// --- GC Helpers ---

void markTable(VM* vm, Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        markValue(vm, entry->key);
        markValue(vm, entry->value);
    }
}

void tableRemoveWhite(Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        // 如果 Key 是对象且未被标记，则删除 (用于弱引用字符串池)
        if (!IS_NIL(entry->key) && 
            IS_OBJ(entry->key) && 
            !AS_OBJ(entry->key)->isMarked) {
            tableDelete(table, entry->key);
        }
    }
}
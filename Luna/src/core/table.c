// src/core/table.c

#include <stdlib.h>
#include "memory.h"
#include "vm/vm.h"

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
// [优化] 核心热路径：查找 Key 对应的 Entry，或者适合插入的空位/墓碑
static INLINE Entry* findEntry(Entry* entries, u32 capacity, Value key) {
    u32 mask = capacity - 1;
    u32 index = valueHash(key) & mask;
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        // [优化] 优先检查空位，这是查找失败的常见情况
        if (IS_NIL(entry->key)) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }
        index = (index + 1) & mask;
    }
}

// --- Internal Helper: Resize ---
static void adjustCapacity(VM* vm, Table* table, u32 capacity) {
    Entry* entries = ALLOCATE(vm, Entry, capacity);
    // 只有当 NIL_VAL 的二进制表示全为 0 时才使用 memset (通常 Value 是 double 或 tagged pointer，可能是 0)
    for (u32 i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    // 重新计算 count (只计算活跃条目，丢弃墓碑，从而“清洗”表)
    table->count = 0;
    u32 mask = capacity - 1;

    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_NIL(entry->key)) continue;
        // 在新数组中，我们确定：
        // 1. 没有重复 Key
        // 2. 没有墓碑
        // 3. 数组还没满
        // 因此不需要调用 findEntry，直接线性寻找第一个空位
        u32 index = valueHash(entry->key) & mask;
        while (!IS_NIL(entries[index].key)) {
            index = (index + 1) & mask;
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
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        u32 capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(vm, table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = IS_NIL(entry->key);
    // 只有当写入真正的空位时，count 才增加
    // 如果是覆盖墓碑 (isNewKey && !IS_NIL(value))，count 不变
    // 因为墓碑之前已经被计入 "非空槽位" 的逻辑中（或者我们接受 count 只代表活跃数，扩容会清理墓碑）
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
    entry->key = NIL_VAL;
    entry->value = BOOL_VAL(true); // Tombstone 标记
    // 注意：删除通常不减少 table->count
    // 这样 findEntry 遇到墓碑时知道还要继续查找，防止链断裂
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

    u32 mask = table->capacity - 1;
    u32 index = hash & mask;

    for (;;) {
        Entry* entry = &table->entries[index];
        if (IS_NIL(entry->key)) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (IS_OBJ(entry->key) && OBJ_TYPE(entry->key) == OBJ_STRING) {
            ObjString* string = AS_STRING(entry->key);
            // [优化] 比较顺序：Hash -> Length -> Char[0] -> memcmp
            // 增加 chars[0] 比较可以大幅减少长字符串调用 memcmp 的次数
            if (string->hash == hash && 
                string->length == length &&
                (length == 0 || (string->chars[0] == chars[0] && memcmp(string->chars, chars, length) == 0))) {
                return string;
            }
        }

        index = (index + 1) & mask;
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

// [重要优化] 原地清理白名单对象，避免 O(N) 的 tableDelete 开销
void tableRemoveWhite(Table* table) {
    u32 capacity = table->capacity;
    Entry* entries = table->entries;
    
    for (u32 i = 0; i < capacity; i++) {
        Entry* entry = &entries[i];
        if (IS_NIL(entry->key)) continue;
        if (IS_OBJ(entry->key) && !AS_OBJ(entry->key)->isMarked) {
            // [优化核心] 直接原地置为墓碑
            entry->key = NIL_VAL;
            entry->value = BOOL_VAL(true);
        }
    }
}
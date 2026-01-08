// src/core/table.h

#include "value.h"

// 前置声明
typedef struct VM VM;

// --- Hash Table Entry ---
typedef struct {
    Value key;
    Value value;
} Entry;

// --- Hash Table ---
// 使用开放寻址法 (Open Addressing) 和 线性探测 (Linear Probing)
typedef struct {
    u32 count;      // 包含 活跃条目 + 墓碑 (Tombstones) 的总数
    u32 capacity;   // 总容量 (必须是 2 的幂)
    Entry* entries;
} Table;

// --- Lifecycle ---
void initTable(Table* table);
void freeTable(VM* vm, Table* table);

// --- Core Operations ---
bool tableGet(Table* table, Value key, Value* value);
bool tableSet(VM* vm, Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(VM* vm, Table* from, Table* to);

// --- String Interning ---
ObjString* tableFindString(Table* table, const char* chars, u32 length, u32 hash);

// --- GC Helpers ---
void markTable(VM* vm, Table* table);
void tableRemoveWhite(Table* table); // 用于弱引用表（如字符串池）的清理
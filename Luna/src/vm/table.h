// src/vm/table.h

#ifndef LUNA_TABLE_H
#define LUNA_TABLE_H

#include "common.h"
#include "value.h"

// 前置声明
typedef struct VM VM;

// --- Hash Table Entry ---
typedef struct {
    ObjString* key;
    Value value;
} Entry;

// --- Hash Table ---
typedef struct {
    u32 count;
    u32 capacity;
    Entry* entries;
} Table;

// --- Lifecycle ---
void initTable(Table* table);
void freeTable(VM* vm, Table* table);

// --- Core Operations ---
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(VM* vm, Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(VM* vm, Table* from, Table* to);

// --- String Interning ---
ObjString* tableFindString(Table* table, const char* chars, int length, u32 hash);

// [新增] GC 辅助函数声明
void markTable(VM* vm, Table* table);
void tableRemoveWhite(Table* table);

#endif
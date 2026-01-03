// src/vm/table.h

#ifndef LUNA_TABLE_H
#define LUNA_TABLE_H

#include "common.h"
#include "value.h"

typedef struct {
    ObjString* key;
    Value value;
} Entry;

typedef struct {
    int count;
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);

// 核心操作：增、删、查
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to); // 用于扩容

// 专门用于查找字符串对象（用于字符串驻留）
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif
// src/core/vm/call_utils.h

#pragma once
#include "vm.h"

// 执行普通闭包调用
bool call(VM* vm, ObjClosure* closure, i32 argCount);

// 通用调用入口 (分发 Native, Class, Closure 等)
bool callValue(VM* vm, Value callee, i32 argCount);

// 绑定方法到实例
bool bindMethod(VM* vm, ObjClass* klass, ObjString* name, Value receiver);

// 处理关键字参数绑定
bool bindKeywordArgs(VM* vm, ObjFunction* function, int argCount, int kwCount);

// 准备带关键字的调用 (参数重排与填充)
bool prepareKeywordCall(VM* vm, ObjFunction* func, int argCount, int kwCount);

// string_util.h

#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include "std/common.h"

size_t slen(const char* str);
int scmp(const char* str1, const char* str2);
char* scpy(char* dest, const char* src);

#endif

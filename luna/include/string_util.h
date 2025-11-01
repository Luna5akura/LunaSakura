// string_util.h

#include "common.h"
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

int slen(const char* str);
int scmp(const char* str1, const char* str2);
char* scpy(char* dest, char* src);
char* sndup(const char* src, size_t n);
char* sdup(const char* src);

#endif
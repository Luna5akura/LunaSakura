// string_util.c

#include "common.h"
#include "mem.h"

int slen(const char* str) {
  int length = 0;
  while (str[length] != '\0') {
    length++;
  }
  return length;
}

int scmp(const char* str1, const char* str2) {
  int i =0;
  while (str1[i] !='\0' && str2[i] != '\0') {
    if (str1[i] != str2[i]) {
      return str1[i] - str2[i];
    }
    i++;
  }
  return str1[i] - str2[i];
}

char* scpy(char* dest, const char* src) {
  int i = 0;
  while (src[i] != '\0') {
    dest[i] = src[i];
    i++;
  }
  dest[i] = '\0';
  return dest;
}

char* sndup(const char* src, size_t n) {
  size_t src_len = slen(src);

  if (n > src_len) {
    n = src_len;
  }

  char* new_str = (char*)mmalloc(n + 1);
  if (!new_str) {
    return nullptr;
  }

  for (size_t i = 0; i < n; i++) {
    new_str[i] = src[i];
  }
  new_str[n] = '\0';

  return new_str;
}

char* sdup(const char* src) {
  size_t src_len = slen(src);

  return sndup(src, src_len);
}

// stdio.h

#include "common.h"

#ifndef STDIO_H
#define STDIO_H

int read(char* buffer, DWORD length);
int wwrite(const char* message, DWORD length);
int writeerr(const char* message, DWORD length);
int pprintf(const char* format, ...);

#endif

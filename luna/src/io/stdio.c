// stdio.c

#include "common.h"
#include "stdarg.h"
#include "stdlib.h"

int read(char* buffer, DWORD length) {
  HANDLE nStdIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD bytesRead;
  if (ReadFile(nStdIn, buffer, length, &bytesRead, NULL)) {
    return bytesRead;
  } else {
    return -1;
  }
}

int wwrite(const char* message, DWORD length) {
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD bytesWritten;
  if (WriteFile(hStdOut, message, length, &bytesWritten, NULL)) {
    return bytesWritten;
  } else {
    return -1;
  }
}

int writeerr(const char* message, DWORD length) {
  HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
  DWORD bytesWritten;
  if (WriteFile(hStdErr, message, length, &bytesWritten, NULL)) {
    return bytesWritten;
  } else {
    return -1;
  }
}

int pprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    char buffer[1024];  // TODO
    char* buf_ptr = buffer;
    const char* ptr;

    for (ptr = format; *ptr != '\0'; ptr++) {
        if (*ptr == '%' && *(ptr + 1) != '\0') {
            ptr++;
            switch (*ptr) {
                case 'd': {
                    int num = va_arg(args, int);
                    buf_ptr = itoa(num, buf_ptr);
                    break;
                }
                case 'f': {
                    double num = va_arg(args, double);
                    buf_ptr = ftoa(num, buf_ptr);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    *buf_ptr++ = c;
                    break;
                }
                case 's': {
                    char* str = va_arg(args, char*);
                    while (*str) {
                        *buf_ptr++ = *str++;
                    }
                    break;
                }
                default: {
                    *buf_ptr++ = '%';
                    *buf_ptr++ = *ptr;
                    break;
                }
            }
        } else {
            *buf_ptr++ = *ptr;
        }
    }

    *buf_ptr = '\0';

    va_end(args);

    return wwrite(buffer, buf_ptr - buffer);
}

// int main() {
//     pprintf("Test integer: %d\n", 42);
//     pprintf("Test character: %c\n", 'A');
//     pprintf("Test string: %s\n", "Hello, World!");
//     return 0;
// }

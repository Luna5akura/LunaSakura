// stdio.c

#include "std/common.h"
#include "std/stdarg.h"
#include "std/stdlib.h"
#include "std/fileio.h"

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
            int width = 0;
            char pad_char = ' ';

            if (*ptr == '0') {
                pad_char = '0';
                ptr++;
            }

            while (*ptr >= '0' && *ptr <= '9') {
                width = width * 10 + (*ptr - '0');
                ptr++;
            }

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
                case 'X': {
                    unsigned int num = va_arg(args, unsigned int);
                    char hex[16];
                    int hex_len = 0;

                    do {
                        int rem = num % 16;
                        hex[hex_len++] = (rem < 10) ? '0' + rem : 'A' + (rem - 10);
                        num /= 16;
                    } while (num > 0);

                    while (hex_len < width) {
                        *buf_ptr++ = pad_char;
                        width--;
                    }

                    while (hex_len > 0) {
                        *buf_ptr++ = hex[--hex_len];
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

int ffprintf(HANDLE hFile, const char* format, ...) {
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
                case 'u': {
                    unsigned int num = va_arg(args, unsigned int);
                    buf_ptr = utoa(num, buf_ptr);
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

    return write_file(hFile, 1, buf_ptr- buffer, buffer);
}



// int main() {
//     pprintf("Test integer: %d\n", 42);
//     pprintf("Test character: %c\n", 'A');
//     pprintf("Test string: %s\n", "Hello, World!");
//     return 0;
// }

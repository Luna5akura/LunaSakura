// stdarg.h

#if defined(__x86_64__) || defined(_M_X64)
    #define PLATFORM_64BIT
#elif defined(__i386__) || defined(_M_IX86)
    #define PLATFORM_32BIT
#else
    #error "Unsupported platform"
#endif

#ifndef STDARG_H
#define STDARG_H

#ifdef PLATFORM_32BIT

typedef char* va_list;

#define _INTSIZEOF(n)    ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))

#define va_start(ap, v)  (ap = (va_list)&v + _INTSIZEOF(v))

#define va_arg(ap, t)    (*(t*)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))

#define va_end(ap)       (ap = (va_list)0)

#endif

#ifdef PLATFORM_64BIT

typedef __builtin_va_list va_list;

#define va_start(ap, last)   __builtin_va_start(ap, last)

#define va_arg(ap, type)     __builtin_va_arg(ap, type)

#define va_end(ap)           __builtin_va_end(ap)

#endif

#endif

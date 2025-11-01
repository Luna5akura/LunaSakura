//common.h

#ifndef COMMON_H
#define COMMON_H

typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned int size_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

#define UINT16_MAX 65535

#define NULL ((void*)0)
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define STR_LEN(s) (sizeof(s) - 1)

extern HANDLE CreateFileA(
    const char* lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    void* lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
);
extern BOOL ReadFile(
    HANDLE hFile,
    void* lpBuffer,
    DWORD nNumberOfBytesToRead,
    DWORD* lpNumberOfBytesRead,
    void* lpOverlapped
);
extern BOOL WriteFile(
    HANDLE hFile,
    const void* lpBuffer,
    DWORD nNumberOfBytesToWrite,
    DWORD* lpNumberOfBytesWritten,
    void* lpOverlapped
);
extern BOOL CloseHandle(HANDLE hObject);
extern HANDLE GetStdHandle(DWORD nStdHandle);

#endif
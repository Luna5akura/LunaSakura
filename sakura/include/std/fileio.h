// fileio.h

#ifndef FILEIO_H
#define FILEIO_H

#include "std/common.h"
#include "wwindows.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

#define EOF (-1)



#define INVALID_SET_FILE_POINTER ((DWORD)-1)

typedef long intptr_t;

extern __stdcall DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, LONG *lpDistanceToMoveHigh, DWORD dwMoveMethod);
intptr_t _get_osfhandle(int fd);
int _fileno(FILE* stream);

HANDLE open_file(const char* filename);
HANDLE open_file_write(const char* filename);
int read_file(HANDLE hFile, size_t size, DWORD count, void* buffer);
int write_file(HANDLE hFile, size_t size, DWORD count, void* buffer);
int close_file(HANDLE hFile);
int seek_file(HANDLE hFile, LONG offset, int whence);
long fftell(FILE *stream);
int ffgetc(FILE *stream);
int ffputc(int c, FILE *file);
#endif

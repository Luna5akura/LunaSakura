// fileio.c

#include "common.h"

HANDLE open_file(const char* filename) {
  HANDLE hFile = CreateFileA(
      filename,
      GENERIC_READ,
      0,
      NULL,
      OPEN_EXISTING,
      0,
      NULL
  );
  return hFile;
}

HANDLE open_file_write(const char* filename) {
  HANDLE hFile = CreateFileA(
    filename,
    GENERIC_WRITE,
    0,
    NULL,
    CREATE_ALWAYS,
    0,
    NULL
  );
  return hFile;
}

int read_file(HANDLE hFile, void* buffer, DWORD length){
  DWORD bytesRead;
  if (ReadFile(hFile, buffer, length, &bytesRead, NULL)) {
    return bytesRead;
  } else {
    return -1;
  }
}

int write_file(HANDLE hFile, const void* buffer, DWORD length) {
  DWORD bytesWritten;
  if (WriteFile(hFile, buffer, length, &bytesWritten, NULL)) {
    return bytesWritten;
  } else {
    return -1;
  }
}
int close_file(HANDLE hFile) {
  if (CloseHandle(hFile)) {
    return 0;
  } else {
    return -1;
  }
}
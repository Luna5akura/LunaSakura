// fileio.c

#include "std/stdio.h"
#include "std/common.h"
#include "util/string_util.h"
#include "wwindows.h"

#include "std/fileio.h"


HANDLE open_file(const char* filename) {
  HANDLE hFile = CreateFileA(
      filename,
      GENERIC_READ,
      FILE_SHARE_READ,
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

int read_file(HANDLE hFile, size_t size, DWORD count, void* buffer) {
  DWORD bytesRead;
  size_t totalBytes = size * count;
  if (ReadFile(hFile, buffer, totalBytes, &bytesRead, NULL)) {
    return bytesRead / size;
  } else {
    return -1;
  }
}

int write_file(HANDLE hFile, size_t size, DWORD count, void* buffer) {
  DWORD bytesWritten;
  size_t totalBytes = size * count;
  if (WriteFile(hFile, buffer, totalBytes, &bytesWritten, NULL)) {
    return bytesWritten / size;
  } else {
    return -1;
  }
}

int seek_file(HANDLE hFile, LONG offset, int whence) {
  LARGE_INTEGER li;
  li.QuadPart = offset;

  switch (whence) {
    case SEEK_SET: {
      li.QuadPart = offset;
      break;
    }
  case SEEK_CUR: {
      if (SetFilePointerEx(hFile, li, &li, FILE_CURRENT) == 0) {
        return -1;
      }
      break;
    }
  case SEEK_END: {
      li.QuadPart = 0;
      if (SetFilePointerEx(hFile, li, &li, FILE_END) == 0) {
        return -1;
      }
      li.QuadPart += offset;
      break;
    }
    default: {
      return -1;
    }
  }
  if (SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) == 0) {
    return -1;
  }

  return 0;
}

int close_file(HANDLE hFile) {
  if (CloseHandle(hFile)) {
    return 0;
  } else {
    return -1;
  }
}

long fftell(FILE *stream) {
  HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(stream));
  if (hFile == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD current_pos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
  if (current_pos == INVALID_SET_FILE_POINTER) {
    return -1;
  }

  return (long)current_pos;
}

int ffgetc(FILE *stream) {
  unsigned char ch;
  size_t result = read_file(&ch, 1, 1, stream);

  if (result == 1) {
    return (int)ch;
  } else {
    return EOF;
  }
}

int ffputc(int c, FILE *file) {
    if (file == NULL) {
        return EOF;
    }

    return write_file(&c, sizeof(char), 1, file) == 1 ? (unsigned char)c : EOF;
}

// int main() {
//     const char* writeFilename = "test_output.txt";
//     const char* readFilename = "test_output.txt";
//     const char* content = "Hello, File I/O!\n";
//
//     // 测试写入文件
//     HANDLE writeFile = open_file_write(writeFilename);
//     if (writeFile != INVALID_HANDLE_VALUE) {
//         write_file(writeFile, sizeof(char), slen(content), (void*)content);
//         close_file(writeFile);
//         pprintf("数据已写入 %s\n", writeFilename);
//     } else {
//         pprintf("无法打开文件 %s 进行写入\n", writeFilename);
//     }
//
//     // 测试读取文件
//     char buffer[256];
//     HANDLE readFile = open_file(readFilename);
//     if (readFile != INVALID_HANDLE_VALUE) {
//         int bytesRead = read_file(readFile, sizeof(char), sizeof(buffer) - 1, buffer);
//         if (bytesRead > 0) {
//             buffer[bytesRead] = '\0'; // 添加字符串终止符
//             pprintf("从文件读取的数据: %s", buffer);
//         } else {
//             pprintf("无法从文件读取数据\n");
//         }
//         close_file(readFile);
//     } else {
//         pprintf("无法打开文件 %s 进行读取\n", readFilename);
//     }
//
//     return 0;
// }
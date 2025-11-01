// nalu.c

#include "std/fileio.h"
#include "std/stdio.h"
#include "std/common.h"

#include "decoder/h264/nalu.h"

int is_start_code(uint8_t *buffer, int length) {
  if (length >= 3 && buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x01) {
    return 3;
  }
  if (length >= 4 && buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x00 && buffer[3] == 0x01) {
    return 4;
  }
  return 0;
}


void free_nalu(NALU_t *nalu) {
    if (nalu) {
        if (nalu->buffer) {
            mfree(nalu->buffer);
        }
        mfree(nalu);
    }
}

NALU_t* read_annexb_nalu(FILE *file) {
    uint8_t buffer[1024 * 1024];
    int buffer_size = sizeof(buffer);

    NALU_t *nalu = mmalloc(sizeof(NALU_t));
    if (!nalu) return NULL;

    nalu->buffer = NULL;
    int total_bytes = 0;
    int start_code_len = 0;

    while (1) {
        int read_bytes = read_file(file, 1, 4, buffer);
        if (read_bytes < 3) {
            free_nalu(nalu);
            return NULL;
        }

        start_code_len = is_start_code(buffer, read_bytes);
        if (start_code_len > 0) {
            seek_file(file, start_code_len - read_bytes, SEEK_CUR);
            break;
        }
    }

    while (total_bytes < buffer_size) {
        int nalu_bytes = read_file(file, 1, buffer_size - total_bytes, buffer + total_bytes);
        if (nalu_bytes <= 0) break;

        total_bytes += nalu_bytes;

        for (int i = start_code_len; i < nalu_bytes - start_code_len; i++) {
            int code_len = is_start_code(buffer + i, nalu_bytes - i);
            if (code_len > 0) {
                seek_file(file, i - nalu_bytes, SEEK_CUR);
                nalu_bytes = i;
                break;
            }
        }

        nalu->buffer = mmalloc(nalu_bytes);
        if (!nalu->buffer) {
            free_nalu(nalu);
            return NULL;
        }

        mcopy(nalu->buffer, buffer, nalu_bytes);
        nalu->nalu_size = nalu_bytes;
        return nalu;
    }

    free_nalu(nalu);
    return NULL;
}


void print_nalu(NALU_t *nalu) {
   for (int i = 0; i < nalu->nalu_size; i++) {
      pprintf("%02X ", nalu->buffer[i]);
      if ((i + 1) % 16 == 0) pprintf("\n");
    }
    pprintf("\n");
}

void read_annexb_nalus(const char *filename) {
  FILE *file = open_file(filename);
  if (!file) {
    pprintf("Error opening the file");
    return;
  }

  int nalu_count = 0;

  while (1) {
    NALU_t *nalu = read_annexb_nalu(file);
    if (!nalu) break;

    nalu_count++;
    pprintf("NALU #%d:\n", nalu_count);
    print_nalu(nalu);
    pprintf("NALU #%d size: %d bytes\n\n", nalu_count, nalu->nalu_size);

    free_nalu(nalu);
  }

  close_file(file);
}

// int main() {
//   const char *filename = "annexb.h264";
//   // const char *filenam = "test.txt";
//   read_annexb_nalus(filename);
//   return 0;
// }

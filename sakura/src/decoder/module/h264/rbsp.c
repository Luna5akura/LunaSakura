// rbsp.c

#include "std/fileio.h"
#include "std/stdio.h"
#include "util/mem.h"

#include "decoder/h264/nalu.h"
#include "decoder/h264/rbsp.h"


void free_rbsp(RBSP_t* rbsp) {
    if (rbsp) {
        if (rbsp->buffer) {
            mfree(rbsp->buffer);
        }
        mfree(rbsp);
    }
}

RBSP_t *nalu_to_rbsp(NALU_t *nalu) {
    RBSP_t *rbsp = (RBSP_t *)mmalloc(sizeof(RBSP_t));
    rbsp->buffer = (uint8_t *)mmalloc(nalu->nalu_size);
    if (!rbsp->buffer) {
        free_rbsp(&rbsp);
        return NULL;
    }

    int rbsp_index = 0;
    for (int i = 0; i < nalu->nalu_size; i++) {
        if (i > 1 && nalu->buffer[i] == 0x03
            && nalu->buffer[i - 1] == 0x00
            && nalu->buffer[i - 2] == 0x00) {
            continue;
        }

        rbsp->buffer[rbsp_index++] = nalu->buffer[i];
    }

    rbsp->rbsp_size = rbsp_index;

    uint8_t rbsp_head = rbsp->buffer[0];

    rbsp->forbidden_zero_bit = (rbsp_head >> 7) & 1;
    rbsp->nal_ref_idc = (rbsp_head >> 5) & 3;
    rbsp->nal_unit_type = (rbsp_head >> 0) & 0x1f;

    return rbsp;
}

void print_rbsp(RBSP_t *rbsp) {
   for (int i = 0; i < rbsp->rbsp_size; i++) {
      pprintf("%02X ", rbsp->buffer[i]);
      if ((i + 1) % 16 == 0) pprintf("\n");
  }
    pprintf("\nForbidden zero bit: %d\n", rbsp->forbidden_zero_bit);
    pprintf("Nal red idc: %d\n", rbsp->nal_ref_idc);
    pprintf("Nal unit type: %d\n", rbsp->nal_unit_type);
  pprintf("\n");
}

// int main() {
//   const char *filename = "annexb.h264";
//   FILE *file = open_file(filename);
//   if (!file) {
//     pprintf("Error opening the file");
//     return;
//   }
//
//   int nalu_count = 0;
//
//   while (1) {
//     NALU_t *nalu = read_annexb_nalu(file);
//     if (!nalu) break;
//
//     nalu_count++;
//     pprintf("NALU #%d:\n", nalu_count);
//     // print_nalu(nalu);
//     pprintf("NALU #%d size: %d bytes\n\n", nalu_count, nalu->nalu_size);
//
//     RBSP_t *rbsp = nalu_to_rbsp(nalu);
//
//     print_rbsp(rbsp);
//
//     free_nalu(nalu);
//   }
//
//   close_file(file);
// }

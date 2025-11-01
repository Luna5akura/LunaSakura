// nalu.h

#ifndef NALU_H
#define NALU_H

#include "std/common.h"

typedef struct {
    int nalu_size;
    uint8_t *buffer;
} NALU_t;

void free_nalu(NALU_t *nalu);
NALU_t* read_annexb_nalu(FILE *file);
void print_nalu(NALU_t *nalu);

#endif

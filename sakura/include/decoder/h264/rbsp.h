// rbsp.h

#ifndef RBSP_H
#define RBSP_H


#include "std/common.h"

typedef struct {
    int rbsp_size;
    uint8_t *buffer;
    int forbidden_zero_bit;
    int nal_ref_idc;
    int nal_unit_type;
} RBSP_t;

void free_rbsp(RBSP_t* rbsp);
RBSP_t *nalu_to_rbsp(NALU_t *nalu);
void print_rbsp(RBSP_t *rbsp);

#endif

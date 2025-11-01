// bitstream.h

#ifndef BITSTREAM_H
#define BITSTREAM_H

#include "std/common.h"

typedef struct {
  uint8_t *data;
  uint32_t size;
  uint32_t byte_pos;
  uint8_t bit_pos;
} Bitstream;

Bitstream *init_bs(Bitstream *bs, uint8_t *data, int size);
int more_rbsp_data(Bitstream *bs);
uint8_t read_bit(Bitstream *bs);
uint32_t read_bits(Bitstream *bs, uint8_t num_bits);
uint32_t read_ue(Bitstream *bs);
int32_t read_se(Bitstream *bs);
uint128_t read_u128(Bitstream *bs);

#endif

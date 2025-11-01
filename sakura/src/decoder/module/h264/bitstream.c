// bitstream.c

#include "decoder/h264/bitstream.h"

#include "util/mem.h"


Bitstream *init_bs(Bitstream *bs, uint8_t *data, int size) {
  bs->byte_pos = 0;
  bs->bit_pos = 0;
  bs->data = mmalloc(sizeof(uint8_t) * size);
  bs->size = size;
  mcopy(bs->data, data, size);
  return bs;
}

int more_rbsp_data(Bitstream *bs) {
  if (bs->byte_pos >= bs->size || (bs->size == bs->byte_pos && (bs->data[bs->byte_pos] >> bs->bit_pos) == 0x80 >> bs->bit_pos)) {
    return 0;
  } else {
    return 1;
  }
}

uint8_t read_bit(Bitstream *bs) {
  if (bs->byte_pos >= bs->size) {
    return 0;
  }
  uint8_t bit = (bs->data[bs->byte_pos] >> (7 - bs->bit_pos)) & 0x1;
  bs->bit_pos++;
  if (bs->bit_pos > 7) {
    bs->bit_pos = 0;
    bs->byte_pos++;
  }
  return bit;
}

uint32_t read_bits(Bitstream *bs, uint8_t num_bits) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < num_bits; i++) {
    value = (value << 1) | read_bit(bs);
  }
  return value;
}

uint32_t read_ue(Bitstream *bs) {
    uint32_t zero_count = 0;
    while (read_bit(bs) == 0 && bs->byte_pos < bs->size) {
        zero_count++;
    }
    uint32_t value = (1 << zero_count) - 1 + read_bits(bs, zero_count);
    return value;
}

int32_t read_se(Bitstream *bs) {
    uint32_t code_num = read_ue(bs);
    int32_t value = (code_num % 2 == 0) ? -(int32_t)(code_num / 2) : (int32_t)((code_num + 1) / 2);
    return value;
}

uint128_t read_u128(Bitstream *bs) {
  uint128_t result;
  uint64_t high1 = (uint64_t)read_bits(bs, 32);
  uint64_t high2 = (uint64_t)read_bits(bs, 32);
  uint64_t low1 = (uint64_t)read_bits(bs, 32);
  uint64_t low2 = (uint64_t)read_bits(bs, 32);
  result.high = (high1 << 32) | high2;
  result.low = (low1 << 32) | low2;

  return result;
}
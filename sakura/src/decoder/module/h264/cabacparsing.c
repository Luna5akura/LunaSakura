// cabacparsing.c

#include "std/common.h"
#include "util/mem.h"
#include "std/math.h"

#include "decoder/h264/bitstream.h"
#include "decoder/h264/sli.h"
#include "decoder/h264/cabacparsing.h"


static const int16_t table_9_13_m[3][13] = {
  {23,  23,  21,  1,   0,   -37, 5,   -13, -11, 1,   12,  -4,  17},
  {22,  34,  16,  -2,  4,   -29, 2,   -6,  -13, 5,   9,   -3,  10},
  {29,  25,  14,  -10 -3,  -27, 26,   -4,  -24, 5,   6,   -17, 14},
};

static const int16_t table_9_13_n[3][13] = {
  {33,  2,   0,   9,   49,  118, 57,  78,  65,  62,  49,  73,  50},
  {25,  0,   0,   9,   41,  118, 65,  71,  79,  52,  50,  70,  54},
  {16,  0,   0,   51,  62,  99,  16,  85,  102, 57,  57,  73,  57},
};

static const int16_t table_9_14_m[3][16] = {
  {18,  9,   29,  26,  16,  9,   -46, 20,  1,   -13, -11, 1,   -6,  -17, -6,  9 },
  {26,  19,  40,  57,  41,  26,  -45, -15, -4,  -6,  -13, 5,   6,   -13, 0,   8 },
  {20,  20,  29,  54,  37,  12,  -32, -22, -2,  -4,  -24, 5,   -6,  -14, -6,  4 },
};

static const int16_t table_9_14_n[3][16] = {
  {64,  43,  0,   67,  90,  104, 127, 104, 67,  78,  65,  62,  86,  95,  61,  45},
  {34,  22,  0,   2,   36,  69,  127, 101, 76,  71,  79,  52,  69,  90,  52,  43},
  {40,  10,  0,   0,   42,  97,  127, 117, 74,  85,  102, 57,  93,  88,  44,  55},
};


static const int16_t table_9_18_m[4][35] = {
  {0,   -4,  -3,  -27, -28, -25, -23, -28, -20, -16, -22, -21, -18, -13, -29, -7,  -5,  -7,
     -13, -3,  -1,  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13},

  {13,  7,   2,   -39, -18, -17, -26, -35, -24, -23, -27, -24, -21, -18, -36, 0,   -5 , -7,
     -4,  0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60},

  {7,   -9,  -20, -36, -17, -14, -25, -25, -12, -17, -31, -14, -18, -13, -37, 11,  5,   2,
     5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21},

  {0,   1,   0,   -17, -13, 0,   -7,  -21, -27, -31, -24, -18, -27, -21, -30, -17, -12, -16,
     -11, -12, -2,  -15, -13, -3,  -8,  -20, -30, -7,  -4,  -5,  -6,  -7,  -8,  -9,  -10},
};

static const int16_t table_9_18_n[4][35] = {
  {45,  78,  96,  126, 98,  101, 67,  82,  94,  83,  110, 91,  102, 93,  127, 92,  89,  96,
     108, 46,  65,  84,  104, 74,  93,  127, 99,  95,  91,  87,  83,  79,  75,  71,  67},

  {15,  51,  80,  127, 91,  96,  81,  98,  102, 97,  119, 99,  110, 102, 127, 80,  89,  94,
     92,  39,  65,  70,  104, 73,  92,  93,  84,  75,  66,  57,  48,  39,  30,  21,  12},

  {34,  88,  127, 127, 91,  95,  84,  86,  89,  91,  127, 76,  103, 90,  127, 80,  76,  84,
     78,  55,  61,  67,  73,  79,  85,  91,  97,  103, 109, 115, 121, 127, 133, 139, 145},

  {11,  55,  69,  127, 102, 82,  74,  107, 127, 127, 127, 95,  127, 114, 127, 123, 115, 122,
     115, 63,  68,  84,  104, 70,  93,  127, 93,  91,  89,  87,  85,  83,  81,  79,  77},
};

static const uint8_t table_ctxIdxInc[22][7] = {
  {8,  10, 10, 10, 10, 10, 10},
  {8,  9,  3,  4,  8,  8,  7 },
  {8,  10, 10, 10, 10, 10, 10},
  {0,  1,  8,  10, 10, 10, 10},
  {0,  9,  1,  2,  8,  3,  3 },
  {0,  1,  2,  10, 10, 10, 10},
  {8,  10, 10, 10, 10, 10, 10},
  {8,  3,  8,  5,  5,  5,  5 },
  {0,  9,  1,  2,  8,  3,  3 },
  {0,  1,  8,  3,  3,  3,  10},
  {8,  3,  4,  5,  6,  6,  6 },
  {8,  3,  4,  5,  6,  6,  6 },
  {8,  4,  5,  5,  5,  5,  5 },
  {8,  2,  3,  3,  3,  3,  3 },
  {8,  3,  3,  10, 10, 10, 10},
  {0,  10, 10, 10, 10, 10, 10},
  {0,  0,  0,  10, 10, 10, 10},
  {8,  10, 10, 10, 10, 10, 10},
  {8,  8,  8,  8,  10, 10, 10},
  {8,  8,  10, 10, 10, 10, 10},
  {0,  10, 10, 10, 10, 10, 10},
  {8,  10, 10, 10, 10, 10, 10},
};

static const uint8_t table_ctxIdxBlockCatOffset[4][6] = {
  {0,  4,  8,  12, 16, 255},
  {0,  15, 29, 44, 47, 0  },
  {0,  15, 29, 44, 47, 0  },
  {0,  10, 20, 30, 39, 0  },
};

static const uint8_t mb_type_I[][26] = {
  {0},
  {1, 0, 0, 0, 0, 0},
  {1, 0, 0, 0, 0, 1},
  {1, 0, 0, 0, 1, 0},
  {1, 0, 0, 0, 1, 1},
  {1, 0, 0, 1, 0, 0, 0},
  {1, 0, 0, 1, 0, 0, 1},
  {1, 0, 0, 1, 0, 1, 0},
  {1, 0, 0, 1, 0, 1, 1},
  {1, 0, 0, 1, 1, 0, 0},
  {1, 0, 0, 1, 1, 0, 1},
  {1, 0, 0, 1, 1, 1, 0},
  {1, 0, 0, 1, 1, 1, 1},
  {1, 0, 1, 0, 0, 0},
  {1, 0, 1, 0, 0, 1},
  {1, 0, 1, 0, 1, 0},
  {1, 0, 1, 0, 1, 1},
  {1, 0, 1, 1, 0, 0, 0},
  {1, 0, 1, 1, 0, 0, 1},
  {1, 0, 1, 1, 0, 1, 0},
  {1, 0, 1, 1, 0, 1, 1},
  {1, 0, 1, 1, 1, 0, 0},
  {1, 0, 1, 1, 1, 0, 1},
  {1, 0, 1, 1, 1, 1, 0},
  {1, 0, 1, 1, 1, 1, 1},
  {1, 1},
};

static const uint8_t mb_type_I_length[26] = {
  1, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 2
};

void get_table_ctxStart(
  uint8_t*** table_m, uint8_t*** table_n, uint16_t* ctxStart, uint8_t* model_cnt,
  CABACTYPE CABACType, SLICETYPE SliceType, uint32_t* cabac_init_idc) {
  if (CABACType == MB_SKIP_FLAG) {
    if (SliceType == P || SliceType == SP) {
      *table_m = table_9_13_m;
      *table_n = table_9_13_n;
      *ctxStart = 11;
      *model_cnt = 3;
    } else if (SliceType == B) {
      *table_m = table_9_14_m;
      *table_n = table_9_14_n;
      *ctxStart = 24;
      *model_cnt = 3;
    }
  } else if (CABACType == MB_FIELD_DECODING_FLAG) {
    if (SliceType == I || SliceType == SI) {
      *cabac_init_idc = 3;
    }
    *table_m = table_9_18_m;
    *table_n = table_9_18_n;
    *ctxStart = 70;
    *model_cnt = 3;
  } else {

  }
}

CABACContextVariable get_context_variable_from_ctxIdx(
  uint16_t ctxIdx, uint8_t** table_m, uint8_t** table_n,
  uint32_t cabac_init_idc, uint32_t SliceQP_Y) {
  uint8_t pStateIdx, valMPS;
  int16_t m, n;
  m = table_m[ctxIdx][cabac_init_idc];
  n = table_n[ctxIdx][cabac_init_idc];
  uint8_t preCtxState = clamp(((m * clamp(SliceQP_Y, 0, 51)) >> 4) + n,1, 126);
  if (preCtxState <= 63) {
    pStateIdx = 63 - preCtxState;
    valMPS = 0;
  } else {
    pStateIdx = preCtxState - 64;
    valMPS = 1;
  }
  CABACContextVariable context_variable;
  context_variable.pStateIdx = pStateIdx;
  context_variable.valMPS = valMPS;
  return context_variable;
}

CABACContextVariable* init_context_variables(CABACTYPE CABACType, SLICETYPE SliceType, uint32_t cabac_init_idc, uint32_t pic_init_qp_minus26, int32_t slice_qp_delta) {
  uint8_t** table_m;
  uint8_t** table_n;
  uint16_t ctxStart;
  uint8_t model_cnt;

  uint32_t SliceQP_Y = (uint32_t)(26 + (int32_t)pic_init_qp_minus26 + slice_qp_delta);
  get_table_ctxStart(&table_m, &table_n, &ctxStart, &model_cnt, CABACType, SliceType, &cabac_init_idc);

  CABACContextVariable* context_variables = (CABACContextVariable*)mmalloc(sizeof(CABACContextVariable) * model_cnt);
  for (uint8_t i = 0; i < model_cnt; i++) {
    context_variables[i] = get_context_variable_from_ctxIdx(ctxStart + i, table_m, table_n, cabac_init_idc, SliceQP_Y);
  }
  return context_variables;
}

ArithmeticDecoder* init_arithmetic_decoder(Bitstream *bs) {
  ArithmeticDecoder *decoder = mmalloc(sizeof(ArithmeticDecoder));
  decoder->codIRange = 0x01FE;
  decoder->codIOffset = read_bits(bs, 9);
  return decoder;
}

void get_syntax_element_info(CABACTYPE CABACType, SyntaxElementInfo *info, SLICETYPE SliceType) {
  switch (CABACType) {
    case MB_SKIP_FLAG: {
      info->binarizationType = BINARIZATION_TYPE_FL;
      info->cMax = 1;
      info->maxBinIdxCtx = 0;
      info->ctxIdxOffset = 24;
      break;
    }
  }
}

void unary_binarization(int value, uint8_t *bins, int *num_bins) {
  int i;
  for (i = 0; i < value; i++) {
    bins[*num_bins] = 1;
    (*num_bins)++;
  }
  bins[*num_bins] = 0;
  (*num_bins)++;
}

void truncated_unary_binarization(int value, int cMax, uint8_t *bins, int *num_bins) {
  int i;
  int bins_to_write = (value < cMax) ? value : cMax;
  for (i = 0; i < bins_to_write; i++) {
    bins[*num_bins] = 1;
    (*num_bins)++;
  }
  if (value < cMax) {
    bins[*num_bins] = 0;
    (*num_bins)++;
  }
}

void uegk_binarization(int value, int k, int signedValFlag, int uCoff, int cMax, uint8_t *bins, int *num_bins) {
  truncated_unary_binarization(min(uCoff, abs(value)), cMax, bins, num_bins);
  if ((signedValFlag == 0 && !(min(uCoff, abs(value)) == uCoff && uCoff >= cMax)) || (signedValFlag == 1 && min(uCoff, abs(value)) == 0)) {
    return;
  }

  if (abs(value) >= uCoff) {
    int sufS = abs(value) - uCoff;
    int stopLoop = 0;
    do {
      if (sufS >= (1 << k)) {
        bins[*num_bins] = 1;
        (*num_bins)++;
        sufS = sufS - (1 << k);
        k++;
      } else {
        bins[*num_bins] = 0;
        (*num_bins)++;
        while (k--) {
          bins[*num_bins] = (sufS >> k ) & 0x01;
          (*num_bins)++;
        }
        stopLoop = 1;
      }
    } while (!stopLoop);
  }
  if (signedValFlag && value != 0) {
    if (value > 0) {
      bins[*num_bins] = 0;
      (*num_bins)++;
    } else {
      bins[*num_bins] = 1;
      (*num_bins)++;
    }
  }
}

void fixed_length_binarization(int value, int cMax, uint8_t *bins, int *num_bins) {
  int i;
  for (i = cMax - 1; i >= 0; i--) {
    bins[*num_bins] = (value >> i) & 1;
    (*num_bins)++;
  }
}

void mb_type_binarization(int mb_type, SLICETYPE slicetype, uint8_t *bins, int *num_bins) {
  switch (slicetype) {
    case SI: {
      bins[*num_bins] = ((mb_type == 0) ? 0 : 1);
      (*num_bins)++;
      if (mb_type != 0) {
        uint8_t suffix_len = mb_type_I_length[mb_type - 1];
        for (uint8_t i = 0; i < suffix_len; i++) {
          bins[*num_bins] = mb_type_I[mb_type - 1][i];
          (*num_bins)++;
        }
      }
      break;
    }
    default: {
      break;
    }
  }
}

uint16_t get_ctdIdx(uint8_t binIdx, uint8_t maxBinIdxCtx, uint8_t ctxIdxOffset, CABACTYPE CABACType) {
  uint8_t x, y;
  if (ctxIdxOffset == 0) {x = 0;}
  else if (ctxIdxOffset == 3) {x = 1;}
  else if (ctxIdxOffset == 11) {x = 2;}
  else if (ctxIdxOffset == 14) {x = 3;}
  else if (ctxIdxOffset == 17) {x = 4;}
  else if (ctxIdxOffset == 21) {x = 5;}
  else if (ctxIdxOffset == 24) {x = 6;}
  else if (ctxIdxOffset == 27) {x = 7;}
  else if (ctxIdxOffset == 32) {x = 8;}
  else if (ctxIdxOffset == 36) {x = 9;}
  else if (ctxIdxOffset == 40) {x = 10;}
  else if (ctxIdxOffset == 47) {x = 11;}
  else if (ctxIdxOffset == 54) {x = 12;}
  else if (ctxIdxOffset == 60) {x = 13;}
  else if (ctxIdxOffset == 64) {x = 14;}
  else if (ctxIdxOffset == 68) {x = 15;}
  else if (ctxIdxOffset == 69) {x = 16;}
  else if (ctxIdxOffset == 70) {x = 17;}
  else if (ctxIdxOffset == 73) {x = 18;}
  else if (ctxIdxOffset == 77) {x = 19;}
  else if (ctxIdxOffset == 276) {x = 20;}
  else if (ctxIdxOffset == 399) {x = 21;}
  else {
    uint8_t ctxBlockCat = 0;
    uint8_t cx = 0;
    // TODO

    uint8_t ctxIdxBlockCatOffset = table_ctxIdxBlockCatOffset[cx][ctxBlockCat];
    return ctxIdxOffset + ctxIdxBlockCatOffset + ctxBlockCat;
  }

  if (binIdx >= 6) {y = 6;}
  else {y = binIdx;}

  uint8_t ctxIdxInc = table_ctxIdxInc[x][y];
  if (ctxIdxInc == 8) {
    return 0; // TODO
  } else if (ctxIdxInc == 9) {
    return 276;
  } else if (ctxIdxInc == 10) {
    return 0; // TODO
  } else {
    return ctxIdxOffset + ctxIdxInc;
  }
}

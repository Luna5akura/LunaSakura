// cabacparsing.h

#ifndef CABACPARSING_H
#define CABACPARSING_H

#include "std/common.h"

typedef struct {
  uint8_t pStateIdx;
  uint8_t valMPS;
} CABACContextVariable;

typedef enum {
  MB_SKIP_FLAG,
  MB_FIELD_DECODING_FLAG,
  MB_TYPE,
  TRANSFORM_SIZE_8x8_FLAG,
  CODED_BLOCK_PATTERN_LUMA,
  CODED_BLOCK_PATTERN_CHROMA,
  MB_QP_DELTA,
} CABACTYPE;

typedef struct {
  uint16_t codIRange;
  uint16_t codIOffset;
} ArithmeticDecoder;

typedef enum {
  BINARIZATION_TYPE_U,
  BINARIZATION_TYPE_TU,
  BINARIZATION_TYPE_UEGk,
  BINARIZATION_TYPE_FL,
  BINARIZATION_TYPE_SE,
} BinarizationType;

typedef struct {
  BinarizationType binarizationType;
  int cMax;
  int k;
  int maxBinIdxCtx;
  int ctxIdxOffset;
  int bypassFlag;
} SyntaxElementInfo;

#endif

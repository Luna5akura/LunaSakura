//math.c

#include "std/common.h"
#include "std/math.h"

int min(int a, int b) {
  if (a > b) {
    return b;
  } else {
    return a;
  }
}

int max(int a, int b) {
  if (a > b) {
    return a;
  } else {
    return b;
  }
}

int clamp(int x, int min, int max) {
  if (x < min) {
    return min;
  } else if (x > max) {
    return max;
  } else {
    return x;
  }
}

int16_t median(int16_t a, int16_t b, int16_t c) {
  if (a > b) {
    if (b > c) return b;
    else if (a > c) return c;
    else return a;
  } else {
    if (a > c) return a;
    else if (b > c) return c;
    else return b;
  }
}

int abs(int x) {
  if (x < 0) return -x;
  return x;
}

double ln(double x) {
    if (x <= 0) {
        return 0.0 / 0.0;
    }

    union {
        double d;
        uint64_t u;
    } un;
    un.d = x;

    int sign = (un.u >> 63) & 1;
    int exponent = (int)((un.u >> 52) & 0x7FF) - 1023;
    uint64_t mantissa = un.u & 0xFFFFFFFFFFFFF;

    double m = 1.0 + (double)mantissa / (1ULL << 52);

    double y = m - 1.0;
    double ln_m = 0.0;
    double term = y;
    double y_pow = y;
    int n;

    int N = 20;

    for (n = 1; n <= N; n++) {
        if (n > 1) {
            y_pow *= -y;
        }
        term = y_pow / n;
        ln_m += term;
    }

    double ln2 = 0.6931471805599453;

    double ln_x = ln_m + exponent * ln2;

    return ln_x;
}

double log(double x, double base) {
    double ln_x = ln(x);
    double ln_base = ln(base);
    return ln_x / ln_base;
}

uint32_t ceil(double x) {
    return (uint32_t)(x + 0.99999999999999);
}
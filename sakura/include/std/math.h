// math.h

#ifndef MATH_H
#define MATH_H

#include "std/common.h"

int min(int a, int b);
int max(int a, int b);
int clamp(int x, int min, int max);
int16_t median(int16_t a, int16_t b, int16_t c);
int abs(int x);
double ln(double x);
double log(double x, double base);
uint32_t ceil(double x);

#endif

// stdlib.c

#include "common.h"
#include "ctype.h"

double atof(const char* str) {
  double result = 0.0;
  double fraction = 1.0;
  bool is_negative = false;
  bool decimal_point_seen = false;

  while (is_whitespace((unsigned char)*str)) {
    str++;
  }

  if (*str == '-') {
    is_negative = true;
    str++;
  } else if (*str == '+') {
    str++;
  }

  while (*str) {
    if (is_digit((unsigned char)*str)) {
      if (decimal_point_seen) {
        fraction /= 10.0;
        result += (*str - '0') * fraction;
      } else {
        result = result * 10.0 + (*str - '0');
      }
    } else if (*str == '.') {
      decimal_point_seen = true;
    } else {
      break;
    }
    str++;
  }

  if (is_negative) {
    result = -result;
  }

  return result;
}


char* itoa(int value, char* str) {
    char* ptr = str;
    if (value < 0) {
        *ptr++ = '-';
        value = -value;
    }
    char* start = ptr;
    do {
        *ptr++ = '0' + (value % 10);
        value /= 10;
    } while (value);
    *ptr = '\0';
    for (char* end = ptr - 1; start < end; start++, end--) {
        char temp = *start;
        *start = *end;
        *end = temp;
    }
    return ptr;
}

char* ftoa(double value, char* str, int precision) {
  char* ptr = str;

  if (value < 0) {
    *ptr++ = '-';
    value = -value;
  }

  int integer_part = (int)value;
  double fractional_part = value - (double)integer_part;

  ptr = itoa(integer_part, ptr);

  if (precision > 0 && fractional_part) {
    *ptr++ = '.';

    for (int i = 0; i < precision; i++) {
      fractional_part *= 10;
      int digit = (int)fractional_part;
      *ptr++ = '0' + digit;
      fractional_part -= digit;
    }
  }

  *ptr = '\0';
  return ptr;
}
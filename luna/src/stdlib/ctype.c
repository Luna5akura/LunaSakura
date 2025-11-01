// ctype.c

#include "string_util.h"

int is_whitespace(char c) {
  return (c == ' '|| c == '\t' || c == '\n' || c == '\r');
}

int is_alpha(char c) {
  return (( c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_');
}

int is_digit(char c) {
  return (c >= '0' && c <= '9');
}

int is_alnum(char c) {
  return (is_alpha(c) || is_digit(c));
}

int is_comparison_operator(const char* op) {
  return (scmp(op, "==") == 0
  || scmp(op, "==") == 0
  || scmp(op, "<=") == 0
  || scmp(op, ">=") == 0
  || scmp(op, "!=") == 0
  || scmp(op, ">") == 0
  || scmp(op, "<") == 0);
}

int is_add_minus(const char* op) {
  return (scmp(op, "+") == 0 || scmp(op, "-") == 0);
}

int is_mul_div(const char* op) {
  return (scmp(op, "*") == 0 || scmp(op, "/") == 0);
}

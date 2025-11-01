// ctype.h

#ifndef CTYPE_H
#define CTYPE_H

int is_whitespace(char c);
int is_alpha(char c);
int is_digit(char c);
int is_alnum(char c);
int is_comparison_operator(const char* op);
int is_add_minus(const char* op);
int is_mul_div(const char* op);

#endif
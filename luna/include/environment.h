// environment.h

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "value.h"

typedef struct sEnvironment {
  struct  sEnvironment* outer;
  int capacity;
  int count;
  char** keys;
  Value* values;
} Environment;

Environment* new_environment(Environment* outer);
void free_environment(Environment* env);

bool environment_set(Environment* env, const char* name, Value value);
bool environment_get(Environment* env, const char* name, Value* value);

#endif

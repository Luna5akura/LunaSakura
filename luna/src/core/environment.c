// environment.c

#include "stdio.h"
#include "mem.h"
#include "string_util.h"

#include "vm.h"
#include "environment.h"

#define INITIAL_CAPACITY 8

Environment* new_environment(Environment* outer) {
    Environment* env = (Environment*)mmalloc(sizeof(Environment));
    env->outer = outer;
    env->capacity = INITIAL_CAPACITY;
    env->count = 0;
    env->keys = (char**)mmalloc(sizeof(char*) * env->capacity);
    env->values = (Value*)mmalloc(sizeof(Value) * env->capacity);
    return env;
}

void free_environment(Environment* env) {
    if (!env) return;
    for (int i = 0; i < env->count; ++i) {
        mfree(env->keys[i]);
    }
    mfree(env->keys);
    mfree(env->values);
    mfree(env);
}

static void environment_resize(Environment* env) {
    env->capacity *= 2;
    env->keys = (char**)mmrealloc(env->keys, sizeof(char*) * env->capacity);
    env->values = (Value*)mmrealloc(env->values, sizeof(Value) * env->capacity);
}

bool environment_set(Environment* env, const char* name, Value value) {
    for (int i = 0; i < env->count; ++i) {
        if (scmp(env->keys[i], name) == 0) {
            env->values[i] = value;
            return false;
        }
    }
    if (env->count == env->capacity) {
        environment_resize(env);
    }
    env->keys[env->count] = sdup(name);
    env->values[env->count] = value;
    env->count++;
    return true;
}

bool environment_get(Environment* env, const char* name, Value* value) {
    for (int i = 0; i < env->count; ++i) {
        if (scmp(env->keys[i], name) == 0) {
            // pprintf("environment_get: Found '%s' at index %d\n", name, i);
            *value = env->values[i];
            return true;
        }
    }
    if (env->outer) {
        return environment_get(env->outer, name, value);
    }
    return false;
}

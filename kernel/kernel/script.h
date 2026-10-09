#ifndef SCRIPT_H
#define SCRIPT_H

#include "types.h"

typedef struct script_val {
    int type; /* 0 = number, 1 = string, 2 = object(reserved), 3 = array(reserved), 4 = function */
    int64_t num;
    char *str;
} script_val_t;

/* Native function signature callable from scripts. The returned value is
 * owned by the script engine (its .str is freed when consumed). */
typedef script_val_t (*script_native_fn)(script_val_t *args, int nargs);

void script_init(void);
int  script_eval(const char *input, script_val_t *result);
int  script_run_file(const char *path);
void script_set_var(const char *name, script_val_t val);
void script_set_const(const char *name, script_val_t val);
int  script_get_var(const char *name, script_val_t *val);

/* Register a native function callable by name from scripts.
 * Returns 0 on success, -1 when the table is full. */
int  script_register_func(const char *name, script_native_fn fn);

/* Representation helpers for array/object string reprs ("[a, b]" / "{k: v}").
 * script_repr_at / script_repr_key return owned script_val_t values. */
int  script_repr_count(const char *s);
script_val_t script_repr_at(const char *s, int64_t idx);
script_val_t script_repr_key(const char *s, const char *key);

#endif
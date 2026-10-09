#ifndef CSL_H
#define CSL_H

#include "types.h"
#include "script.h"

/* CSL — CodeOS Scripting Language
 * A user-friendly scripting language built on top of script.c
 * Supports .csl files, built-in functions, and shell integration */

#define CSL_MAX_ARGS    16
#define CSL_LINE_MAX    512
#define CSL_VAR_NAME_MAX 32

typedef enum {
    CSL_TYPE_NUM,
    CSL_TYPE_STR,
} csl_type_t;

typedef struct {
    csl_type_t type;
    int64_t num;
    char str[256];
} csl_val_t;

int  csl_init(void);
int  csl_eval(const char *line);
int  csl_run_file(const char *path);
int  csl_set_var(const char *name, csl_val_t val);
int  csl_get_var(const char *name, csl_val_t *val);
void csl_print(const char *s);
void csl_print_num(int64_t n);

/* Built-in functions: register a native function callable from scripts.
 * fn receives an owned args array (up to CSL_MAX_ARGS) and must return an
 * owned script_val_t. Returns 0 on success, -1 when the table is full. */
int  csl_register_func(const char *name, script_native_fn fn);

#endif

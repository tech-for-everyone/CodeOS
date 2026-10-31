#ifndef JENGINE_H
#define JENGINE_H

#include <stdint.h>

typedef struct {
    int type; /* 0=number, 1=string, 2=boolean, 3=null, 4=undefined */
    int64_t num_val;
    char *str_val;
} jengine_value;

#define JENGINE_NUMBER    0
#define JENGINE_STRING    1
#define JENGINE_BOOLEAN   2
#define JENGINE_NULL      3
#define JENGINE_UNDEFINED 4
#define JENGINE_ERROR     5

/* Script execution can be kept native or delegated to a browser runtime.
 * Gecko is intentionally an opt-in backend: selecting it without registering
 * a bridge returns JENGINE_BACKEND_UNAVAILABLE instead of silently executing
 * browser code in the tiny kernel evaluator. */
#define JENGINE_BACKEND_NATIVE 0
#define JENGINE_BACKEND_GECKO  1
#define JENGINE_BACKEND_UNAVAILABLE (-2)

typedef int (*jengine_host_eval_fn)(void *user_data, const char *source,
                                    char *output, int output_size);

jengine_value jengine_eval(const char *source);
void jengine_set_backend(int backend);
int jengine_get_backend(void);
void jengine_set_host_eval(jengine_host_eval_fn callback, void *user_data);
int jengine_eval_script(const char *source, char *output, int output_size);
void jengine_init(void);
void jengine_cleanup(void);

#endif

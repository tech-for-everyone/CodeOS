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

jengine_value jengine_eval(const char *source);
void jengine_init(void);
void jengine_cleanup(void);

#endif

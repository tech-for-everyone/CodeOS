#ifndef OW_ENGINE_H
#define OW_ENGINE_H

#include "jengine/jengine.h"

#define OW_ENGINE_LITEHTML 0
#define OW_ENGINE_GECKO   1

typedef struct {
    int backend;
    int gecko_ready;
} ow_engine_status_t;

void ow_engine_init(void);
int ow_engine_select(int backend);
int ow_engine_backend(void);
void ow_engine_status(ow_engine_status_t *status);
void ow_engine_set_gecko_bridge(jengine_host_eval_fn callback, void *user_data);
int ow_engine_eval_script(const char *source, char *output, int output_size);

#endif
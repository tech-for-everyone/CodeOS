#include "ow_engine.h"

static int g_backend = OW_ENGINE_LITEHTML;
static int g_gecko_ready;

void ow_engine_init(void) {
    g_backend = OW_ENGINE_LITEHTML;
    g_gecko_ready = 0;
    jengine_set_backend(JENGINE_BACKEND_NATIVE);
}

int ow_engine_select(int backend) {
    if (backend == OW_ENGINE_GECKO) {
        if (!g_gecko_ready) return JENGINE_BACKEND_UNAVAILABLE;
        g_backend = backend;
        jengine_set_backend(JENGINE_BACKEND_GECKO);
        return 0;
    }
    g_backend = OW_ENGINE_LITEHTML;
    jengine_set_backend(JENGINE_BACKEND_NATIVE);
    return 0;
}

int ow_engine_backend(void) { return g_backend; }

void ow_engine_status(ow_engine_status_t *status) {
    if (!status) return;
    status->backend = g_backend;
    status->gecko_ready = g_gecko_ready;
}

void ow_engine_set_gecko_bridge(jengine_host_eval_fn callback, void *user_data) {
    jengine_set_host_eval(callback, user_data);
    g_gecko_ready = callback != 0;
}

int ow_engine_eval_script(const char *source, char *output, int output_size) {
    return jengine_eval_script(source, output, output_size);
}

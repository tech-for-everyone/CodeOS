#include "openweb_gecko_adapter.h"
#include "ow_engine.h"

static int g_ready;
static openweb_gecko_adapter_t g_adapter;

int openweb_gecko_adapter_install(const openweb_gecko_adapter_t *adapter) {
    if (!adapter || !adapter->eval_script || !adapter->load_page || !adapter->pump) {
        g_ready = 0;
        g_adapter.eval_script = 0;
        g_adapter.load_page = 0;
        g_adapter.pump = 0;
        g_adapter.shutdown = 0;
        g_adapter.user_data = 0;
        ow_engine_set_gecko_bridge(0, 0);
        ow_engine_select(OW_ENGINE_LITEHTML);
        return -1;
    }
    g_adapter = *adapter;
    ow_engine_set_gecko_bridge(adapter->eval_script, adapter->user_data);
    if (ow_engine_select(OW_ENGINE_GECKO) != 0) {
        ow_engine_set_gecko_bridge(0, 0);
        g_adapter.eval_script = 0;
        g_adapter.load_page = 0;
        g_adapter.pump = 0;
        g_adapter.shutdown = 0;
        g_adapter.user_data = 0;
        return -1;
    }
    g_ready = 1;
    return 0;
}

int openweb_gecko_adapter_start(const char *url, char *html, int html_size) {
    if (!g_ready || !g_adapter.load_page || !url || !html || html_size <= 0) return -1;
    return g_adapter.load_page(g_adapter.user_data, url, html, html_size);
}

int openweb_gecko_adapter_pump(void) {
    if (!g_ready || !g_adapter.pump) return -1;
    return g_adapter.pump(g_adapter.user_data);
}

void openweb_gecko_adapter_shutdown(void) {
    if (g_adapter.shutdown) g_adapter.shutdown(g_adapter.user_data);
    g_ready = 0;
    ow_engine_set_gecko_bridge(0, 0);
    ow_engine_select(OW_ENGINE_LITEHTML);
}

int openweb_gecko_adapter_ready(void) { return g_ready; }

#ifndef OPENWEB_GECKO_ADAPTER_H
#define OPENWEB_GECKO_ADAPTER_H

#include "jengine/jengine.h"

typedef int (*openweb_gecko_load_fn)(void *user_data, const char *url,
                                     char *html, int html_size);
typedef int (*openweb_gecko_pump_fn)(void *user_data);
typedef void (*openweb_gecko_shutdown_fn)(void *user_data);

/* Eclipse integration contract. The Eclipse host owns Gecko's event loop and
 * supplies these callbacks; OpenWeb never links Gecko into the kernel image. */
typedef struct {
    jengine_host_eval_fn eval_script;
    openweb_gecko_load_fn load_page;
    openweb_gecko_pump_fn pump;
    openweb_gecko_shutdown_fn shutdown;
    void *user_data;
} openweb_gecko_adapter_t;

int openweb_gecko_adapter_install(const openweb_gecko_adapter_t *adapter);
int openweb_gecko_adapter_start(const char *url, char *html, int html_size);
int openweb_gecko_adapter_pump(void);
void openweb_gecko_adapter_shutdown(void);
int openweb_gecko_adapter_ready(void);

#endif

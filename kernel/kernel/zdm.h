#ifndef ZDM_H
#define ZDM_H

#include "types.h"

/* Zircon Debug Mode — lightweight debug event logger for CodeOS→Zircon debugging */

#define ZDM_EVENT_MAX  64
#define ZDM_TEXT_LEN   80

typedef enum {
    ZDM_NOTIFICATION,
    ZDM_IPC_SEND,
    ZDM_IPC_RECV,
    ZDM_APP_LAUNCH,
    ZDM_APP_CLOSE,
    ZDM_KEY_EVENT,
    ZDM_DRAWER_TOGGLE,
    ZDM_SHADE_TOGGLE,
    ZDM_QS_TOGGLE,
    ZDM_INIT,
} zdm_event_type_t;

typedef struct {
    uint64_t         timestamp;
    zdm_event_type_t type;
    char             text[ZDM_TEXT_LEN];
} zdm_event_t;

/* Enable/disable debug logging */
void zdm_set_enabled(int en);
int  zdm_is_enabled(void);

/* Log an event */
void zdm_log(zdm_event_type_t type, const char *text);

/* Dump all events via kprintf */
void zdm_dump(void);

/* Clear event buffer */
void zdm_clear(void);

/* Access raw events for overlay drawing */
int  zdm_event_count(void);
const zdm_event_t *zdm_event_at(int idx);

#endif

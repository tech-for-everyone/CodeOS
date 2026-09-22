#include "zdm.h"
#include "kprintf.h"
#include "string.h"
#include "../drivers/timer.h"
#include "../arch/x86_64/fb.h"
#include "pixelman.h"
#include "panels.h"
#include "windows.h"

static int zdm_enabled;
static zdm_event_t zdm_events[ZDM_EVENT_MAX];
static int zdm_head;
static int zdm_count;

void zdm_set_enabled(int en) {
    zdm_enabled = en;
    kprintf("zdm: debug %s\n", en ? "ON" : "OFF");
}

int zdm_is_enabled(void) {
    return zdm_enabled;
}

static const char *type_name(zdm_event_type_t t) {
    switch (t) {
        case ZDM_NOTIFICATION:   return "NOTIF";
        case ZDM_IPC_SEND:       return "IPC>>";
        case ZDM_IPC_RECV:       return "IPC<<";
        case ZDM_APP_LAUNCH:     return "LAUNCH";
        case ZDM_APP_CLOSE:      return "CLOSE";
        case ZDM_KEY_EVENT:      return "KEY";
        case ZDM_DRAWER_TOGGLE:  return "DRAWER";
        case ZDM_SHADE_TOGGLE:   return "SHADE";
        case ZDM_QS_TOGGLE:      return "QS";
        case ZDM_INIT:           return "INIT";
        default:                 return "?";
    }
}

void zdm_log(zdm_event_type_t type, const char *text) {
    if (!zdm_enabled) return;
    zdm_event_t *ev = &zdm_events[zdm_head];
    ev->timestamp = timer_get_milliseconds();
    ev->type = type;
    int i = 0;
    while (text[i] && i < ZDM_TEXT_LEN - 1) {
        ev->text[i] = text[i];
        i++;
    }
    ev->text[i] = 0;
    zdm_head = (zdm_head + 1) % ZDM_EVENT_MAX;
    if (zdm_count < ZDM_EVENT_MAX) zdm_count++;
}

void zdm_dump(void) {
    kprintf("── ZDM: %d events ──\n", zdm_count);
    int n = zdm_count;
    int start = (zdm_count < ZDM_EVENT_MAX) ? 0 : zdm_head;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % ZDM_EVENT_MAX;
        zdm_event_t *ev = &zdm_events[idx];
        uint64_t ms = ev->timestamp;
        kprintf("  [%5llu.%03u] %-6s %s\n",
                ms / 1000, (unsigned)(ms % 1000),
                type_name(ev->type), ev->text);
    }
}

void zdm_clear(void) {
    zdm_head = 0;
    zdm_count = 0;
}

int zdm_event_count(void) {
    return zdm_count;
}

const zdm_event_t *zdm_event_at(int idx) {
    if (idx < 0 || idx >= zdm_count) return NULL;
    int start = (zdm_count < ZDM_EVENT_MAX) ? 0 : zdm_head;
    return &zdm_events[(start + idx) % ZDM_EVENT_MAX];
}

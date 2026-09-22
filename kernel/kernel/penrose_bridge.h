#ifndef PENROSE_BRIDGE_H
#define PENROSE_BRIDGE_H

#include <stdint.h>

#define PRS_QUEUE_MAX 64

typedef enum {
    PRS_EVENT_NONE = 0,
    PRS_EVENT_MAP_REQUEST = 1,
    PRS_EVENT_UNMAP_NOTIFY = 2,
    PRS_EVENT_DESTROY_NOTIFY = 3,
    PRS_EVENT_CONFIGURE_REQUEST = 4,
    PRS_EVENT_KEY_PRESS = 5,
    PRS_EVENT_KEY_RELEASE = 6,
    PRS_EVENT_BUTTON_PRESS = 7,
    PRS_EVENT_BUTTON_RELEASE = 8,
    PRS_EVENT_MOTION = 9,
} prs_event_type_t;

typedef struct {
    int type;
    uint32_t window;
    int detail;
    uint16_t state;
    int16_t mx, my;
    int16_t cx, cy, cw, ch;
} prs_event_t;

void prs_init(void);

prs_event_t prs_event_pop(void);
int prs_event_count(void);

uint32_t prs_root(void);
int prs_screen_count(void);
int prs_screen_geom(int i, int *x, int *y, int *w, int *h);
int prs_existing_clients(uint32_t *buf, int max);
int prs_client_geom(uint32_t xid, int *x, int *y, int *w, int *h);
int prs_client_title(uint32_t xid, char *buf, int max);
uint32_t prs_client_pid(uint32_t xid);
int prs_client_float(uint32_t xid, const char *class_name);
int prs_client_managed(uint32_t xid);
int prs_client_fullscreen(uint32_t xid);

void prs_position_client(uint32_t xid, int x, int y, int w, int h);
void prs_show_client(uint32_t xid);
void prs_hide_client(uint32_t xid);
void prs_withdraw_client(uint32_t xid);
void prs_focus_client(uint32_t xid);
void prs_kill_client(uint32_t xid);

int prs_compositor_idx(uint32_t xid);
void prs_blit(uint32_t xid, int dst_x, int dst_y, int w, int h,
              const void *pixels, int stride);
void prs_set_title(uint32_t xid, const char *title);
int prs_painting(void);

/* ── desktop chrome: X11 windows as first-class desktop citizens ──
 * The HyperDE shell (bar pills + window chrome) consumes this snapshot;
 * Qt routes bar-pill clicks, chrome clicks and keyboard shortcuts through
 * the same API. */

#define PRS_CHROME_SH 6  /* shadow inset, mirrors rust_hyperde WIN_SH */
#define PRS_CHROME_TB 30 /* title-band height, mirrors rust_hyperde WIN_TB */
#define PRS_WIN_MAX   32

typedef struct {
    uint32_t xid;
    int16_t x, y, w, h;
    uint8_t mapped;   /* 1 = mapped and composited */
    uint8_t focused;  /* 1 = active window */
    char title[40];
} prs_desktop_win_t;

/* Snapshot mapped X11 windows in creation order (x11 table order). Returns
 * the number written (≤ max). Order is stable within one frame. */
int prs_desktop_windows(prs_desktop_win_t *buf, int max);

/* xid of the currently focused X11 window, or 0. */
uint32_t prs_desktop_focused(void);

/* Title-band hit-test for chrome clicks. Returns the xid whose chrome is
 * under (mx,my), or 0. On a hit, *ctl is set: 1=close, 2=minimize,
 * 3=maximize, 0=title band (focus). Body clicks (below the band) also
 * return the xid with *ctl=-1. */
uint32_t prs_window_at(int mx, int my, int *ctl);

/* Minimize (hide + UNMAP, compositor entry kept for restore). */
void prs_minimize_client(uint32_t xid);

void prs_emit_map_request(uint32_t xid);
void prs_emit_unmap(uint32_t xid);
void prs_emit_destroy(uint32_t xid);
void prs_emit_configure(uint32_t xid, int x, int y, int w, int h);
void prs_emit_key(int press, uint32_t keycode, uint16_t state);
void prs_emit_button(int press, uint32_t button, uint16_t state, int mx, int my);
void prs_emit_motion(int mx, int my, uint16_t state);

uint32_t prs_demo_window(const char *title);
void prs_demo_spawn(void);

#endif
// penrose_bridge.c
//
// Kernel side of the penrose window-manager bridge.
//
// The X11 server (x11_server.c) turns protocol requests from X clients into
// penrose WM events queued here. penrose (running as a no_std Rust staticlib)
// consumes those events and calls back into the functions below to position,
// map, unmap, focus and kill X11 windows.
//
// Window geometry is applied to both the X11 server's window table and the
// HyperDE compositor (xserver.cpp) which owns actual presentation.
#include "penrose_bridge.h"
#include "kprintf.h"
#include "x11_server.h"
#include "xserver.h"

#include <string.h>

#define PRS_XS_MAX 32

typedef struct {
    uint32_t xid;
    int xs_idx;
} prs_xs_entry_t;

static prs_event_t g_queue[PRS_QUEUE_MAX];
static int g_qhead = 0;
static int g_qtail = 0;
static int g_qcount = 0;

static prs_xs_entry_t g_xs[PRS_XS_MAX];
static int g_xs_count = 0;

static xs_screen_t *g_screen = 0;

void prs_init(void) {
    g_qhead = g_qtail = g_qcount = 0;
    g_xs_count = 0;
    g_screen = xs_get_screen();
}

prs_event_t prs_event_pop(void) {
    prs_event_t ev;
    if (g_qcount <= 0) {
        memset(&ev, 0, sizeof(ev));
        ev.type = PRS_EVENT_NONE;
        return ev;
    }
    ev = g_queue[g_qhead];
    g_qhead = (g_qhead + 1) % PRS_QUEUE_MAX;
    g_qcount--;
    return ev;
}

int prs_event_count(void) {
    return g_qcount;
}

static void push_event(prs_event_t *ev) {
    if (g_qcount >= PRS_QUEUE_MAX) return;
    g_queue[g_qtail] = *ev;
    g_qtail = (g_qtail + 1) % PRS_QUEUE_MAX;
    g_qcount++;
}

uint32_t prs_root(void) {
    x11_client_t *c = x11_get_client(1);
    if (c && c->root_window) return c->root_window;
    x11_window_t *w = x11_get_window(0);
    return w ? w->xid : 0;
}

int prs_screen_count(void) {
    return 1;
}

int prs_screen_geom(int i, int *x, int *y, int *w, int *h) {
    if (i != 0 || !g_screen) return -1;
    *x = 0;
    *y = 28;            // HyperDE top bar
    *w = g_screen->width;
    *h = g_screen->height - 28;
    return 0;
}

int prs_existing_clients(uint32_t *buf, int max) {
    int n = 0;
    for (int i = 0; i < X11_MAX_WINDOWS && n < max; i++) {
        uint32_t xid = x11_window_xid(i);
        if (xid) buf[n++] = xid;
    }
    return n;
}

int prs_client_geom(uint32_t xid, int *x, int *y, int *w, int *h) {
    x11_window_t *xw = x11_get_window(xid);
    if (!xw) return -1;
    *x = xw->x;
    *y = xw->y;
    *w = xw->width;
    *h = xw->height;
    return 0;
}

int prs_client_title(uint32_t xid, char *buf, int max) {
    x11_window_t *w = x11_get_window(xid);
    if (!w) return -1;
    if (max <= 0) return 0;
    int n = 0;
    while (w->title[n] && n < max - 1) {
        buf[n] = w->title[n];
        n++;
    }
    buf[n] = 0;
    return n;
}

uint32_t prs_client_pid(uint32_t xid) {
    x11_window_t *w = x11_get_window(xid);
    if (!w) return 0;
    x11_client_t *c = x11_get_client(w->client_id);
    return c ? (uint32_t)c->pid : 0;
}

int prs_client_float(uint32_t xid, const char *class_name) {
    (void)xid;
    if (!class_name) return 0;
    return (strcmp(class_name, "dmenu") == 0 || strcmp(class_name, "dunst") == 0);
}

int prs_client_managed(uint32_t xid) {
    x11_window_t *w = x11_get_window(xid);
    return w && w->mapped;
}

int prs_client_fullscreen(uint32_t xid) {
    (void)xid;
    return 0;
}

int prs_compositor_idx(uint32_t xid) {
    for (int i = 0; i < g_xs_count; i++) {
        if (g_xs[i].xid == xid) return g_xs[i].xs_idx;
    }
    return -1;
}

static int add_compositor(uint32_t xid, const char *title, int x, int y, int w, int h) {
    if (!g_screen) return -1;
    int idx = prs_compositor_idx(xid);
    if (idx >= 0) return idx;

    idx = xs_create_window(g_screen, x, y, w, h, title, 0xFF202028);
    if (idx < 0) return -1;
    if (g_xs_count < PRS_XS_MAX) {
        g_xs[g_xs_count].xid = xid;
        g_xs[g_xs_count].xs_idx = idx;
        g_xs_count++;
    }
    return idx;
}

void prs_position_client(uint32_t xid, int x, int y, int w, int h) {
    x11_window_t *xw = x11_get_window(xid);
    if (!xw) return;
    kprintf("PRS pos xid=%u '%s' @%d,%d %dx%d\n",
            xid, xw->title ? xw->title : "", x, y, w, h);
    xw->x = x;
    xw->y = y;
    xw->width = w;
    xw->height = h;

    int idx = prs_compositor_idx(xid);
    if (idx >= 0 && g_screen) {
        xs_move_window(g_screen, idx, x, y);
        xs_resize_window(g_screen, idx, w, h);
    }
}

void prs_show_client(uint32_t xid) {
    x11_window_t *xw = x11_get_window(xid);
    if (!xw) return;

    int idx = add_compositor(xid, xw->title, xw->x, xw->y, xw->width, xw->height);
    if (idx >= 0 && g_screen) {
        xs_raise_window(g_screen, idx);
        xs_show_window(g_screen, idx, 1);
    }

    if (!xw->mapped) {
        xw->mapped = 1;
        x11_event_t ev = {0};
        ev.type = X11_EVENT_MAP_NOTIFY;
        ev.event = xid;
        x11_broadcast_event(&ev, -1);
    }
}

void prs_hide_client(uint32_t xid) {
    int idx = prs_compositor_idx(xid);
    if (idx >= 0 && g_screen) xs_show_window(g_screen, idx, 0);

    x11_window_t *xw = x11_get_window(xid);
    if (xw && xw->mapped) {
        xw->mapped = 0;
        x11_event_t ev = {0};
        ev.type = X11_EVENT_UNMAP_NOTIFY;
        ev.event = xid;
        x11_broadcast_event(&ev, -1);
    }
}

void prs_withdraw_client(uint32_t xid) {
    int idx = prs_compositor_idx(xid);
    if (idx >= 0 && g_screen) xs_show_window(g_screen, idx, 0);
    x11_window_t *xw = x11_get_window(xid);
    if (xw) xw->mapped = 0;
}

void prs_focus_client(uint32_t xid) {
    int idx = prs_compositor_idx(xid);
    if (idx >= 0 && g_screen) {
        xs_set_active_window(g_screen, idx);
        xs_raise_window(g_screen, idx);
    }
}

void prs_kill_client(uint32_t xid) {
    int idx = prs_compositor_idx(xid);
    if (idx >= 0 && g_screen) xs_destroy_window(g_screen, idx);
    for (int i = 0; i < g_xs_count; i++) {
        if (g_xs[i].xid == xid) {
            g_xs[i] = g_xs[g_xs_count - 1];
            g_xs_count--;
            break;
        }
    }

    x11_release_window(xid);
    x11_event_t ev = {0};
    ev.type = X11_EVENT_DESTROY_NOTIFY;
    ev.event = xid;
    x11_broadcast_event(&ev, -1);
}

/* ── desktop chrome API (bar pills / window chrome / click routing) ── */

int prs_desktop_windows(prs_desktop_win_t *buf, int max) {
    if (!buf || max <= 0) return 0;
    int n = 0;
    int active = (g_screen) ? xs_active_window(g_screen) : -1;
    for (int i = 0; i < X11_MAX_WINDOWS && n < max; i++) {
        x11_window_t *xw = x11_get_window(x11_window_xid(i));
        if (!xw) continue;
        if (!xw->mapped) continue;
        prs_desktop_win_t *d = &buf[n++];
        d->xid = xw->xid;
        d->x = (int16_t)xw->x;
        d->y = (int16_t)xw->y;
        d->w = (int16_t)xw->width;
        d->h = (int16_t)xw->height;
        d->mapped = 1;
        d->focused = (active >= 0 && prs_compositor_idx(xw->xid) == active) ? 1 : 0;
        int t = 0;
        while (xw->title[t] && t < (int)sizeof(d->title) - 1) {
            d->title[t] = xw->title[t];
            t++;
        }
        d->title[t] = 0;
    }
    return n;
}

uint32_t prs_desktop_focused(void) {
    if (!g_screen) return 0;
    int idx = xs_active_window(g_screen);
    if (idx < 0) return 0;
    for (int i = 0; i < g_xs_count; i++) {
        if (g_xs[i].xs_idx == idx) return g_xs[i].xid;
    }
    return 0;
}

void prs_minimize_client(uint32_t xid) {
    prs_hide_client(xid); /* hide + UNMAP notify, compositor entry kept */
}

uint32_t prs_window_at(int mx, int my, int *ctl) {
    if (ctl) *ctl = 0;
    if (!g_screen) return 0;
    uint32_t best = 0;
    int best_z = -1;
    int best_ctl = 0;
    for (int i = 0; i < X11_MAX_WINDOWS; i++) {
        x11_window_t *xw = x11_get_window(x11_window_xid(i));
        if (!xw || !xw->mapped) continue;
        int x = xw->x, y = xw->y, w = xw->width, h = xw->height;
        if (w <= 0 || h <= 0) continue;
        if (mx < x || mx >= x + w || my < y || my >= y + h) continue;
        int idx = prs_compositor_idx(xw->xid);
        int z = 0;
        xs_window_t *xs = (idx >= 0) ? xs_get_window(g_screen, idx) : 0;
        if (xs) z = xs->z_index;
        if (best && z < best_z) continue;

        int ctl2 = -1;
        int band = y + PRS_CHROME_SH;
        if (my >= band && my < band + PRS_CHROME_TB) {
            int dot_y = band + (PRS_CHROME_TB - 12) / 2 + 6;
            int close_x = x + w - PRS_CHROME_SH - 18;
            int min_x = close_x - 20;
            int max_x = close_x - 40;
            if (my >= dot_y - 9 && my <= dot_y + 9) {
                if (mx >= max_x - 9 && mx <= max_x + 9) ctl2 = 3;
                else if (mx >= min_x - 9 && mx <= min_x + 9) ctl2 = 2;
                else if (mx >= close_x - 9 && mx <= close_x + 9) ctl2 = 1;
            }
            if (ctl2 < 0) ctl2 = 0;
        }
        best = xw->xid;
        best_z = z;
        best_ctl = ctl2;
    }
    if (ctl && best) *ctl = best_ctl;
    return best;
}

void prs_blit(uint32_t xid, int dst_x, int dst_y, int w, int h, const void *pixels, int stride) {
    int idx = prs_compositor_idx(xid);
    if (idx < 0 || !g_screen || !pixels) return;
    xs_set_window_pixels(g_screen, idx, (const uint32_t *)pixels, stride,
                         dst_x, dst_y, w, h);
}

void prs_set_title(uint32_t xid, const char *title) {
    x11_window_t *xw = x11_get_window(xid);
    if (!xw || !title) return;
    int n = 0;
    while (title[n] && n < (int)sizeof(xw->title) - 1) {
        xw->title[n] = title[n];
        n++;
    }
    xw->title[n] = 0;
    int idx = prs_compositor_idx(xid);
    if (idx >= 0 && g_screen) xs_set_window_title(g_screen, idx, xw->title);
}

int prs_painting(void) {
    return g_qcount > 0;
}

// ---- event generation (called from x11_server.c) ----

void prs_emit_map_request(uint32_t xid) {
    prs_event_t ev = {0};
    ev.type = PRS_EVENT_MAP_REQUEST;
    ev.window = xid;
    push_event(&ev);
}

void prs_emit_unmap(uint32_t xid) {
    prs_event_t ev = {0};
    ev.type = PRS_EVENT_UNMAP_NOTIFY;
    ev.window = xid;
    push_event(&ev);
}

void prs_emit_destroy(uint32_t xid) {
    prs_event_t ev = {0};
    ev.type = PRS_EVENT_DESTROY_NOTIFY;
    ev.window = xid;
    push_event(&ev);
}

void prs_emit_configure(uint32_t xid, int x, int y, int w, int h) {
    prs_event_t ev = {0};
    ev.type = PRS_EVENT_CONFIGURE_REQUEST;
    ev.window = xid;
    ev.cx = (int16_t)x;
    ev.cy = (int16_t)y;
    ev.cw = (int16_t)w;
    ev.ch = (int16_t)h;
    push_event(&ev);
}

void prs_emit_key(int press, uint32_t keycode, uint16_t state) {
    prs_event_t ev = {0};
    ev.type = press ? PRS_EVENT_KEY_PRESS : PRS_EVENT_KEY_RELEASE;
    ev.detail = (int)keycode;
    ev.state = state;
    push_event(&ev);
}

void prs_emit_button(int press, uint32_t button, uint16_t state, int mx, int my) {
    prs_event_t ev = {0};
    ev.type = press ? PRS_EVENT_BUTTON_PRESS : PRS_EVENT_BUTTON_RELEASE;
    ev.detail = (int)button;
    ev.state = state;
    ev.mx = (int16_t)mx;
    ev.my = (int16_t)my;
    push_event(&ev);
}

void prs_emit_motion(int mx, int my, uint16_t state) {
    prs_event_t ev = {0};
    ev.type = PRS_EVENT_MOTION;
    ev.mx = (int16_t)mx;
    ev.my = (int16_t)my;
    ev.state = state;
    push_event(&ev);
}

// ---- self-test demo windows ----
//
// Spawns a window through the same bridge path a real X11 client would take:
// a window is created and immediately "mapped" which queues a MapRequest for
// the window manager to pick up, tile and present.

uint32_t prs_demo_window(const char *title) {
    uint32_t xid = x11_create_kernel_window(title, 480, 360);
    if (!xid) return 0;
    prs_emit_map_request(xid);
    return xid;
}

void prs_demo_spawn(void) {
    const char *names[] = { "penrose shell", "codeos files", "openweb" };
    for (int i = 0; i < 3; i++) {
        prs_demo_window(names[i]);
    }
}
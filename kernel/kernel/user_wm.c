/* user_wm.c — user-process window bridge (Android apps on the desktop).
 *
 * Maps WM-protocol messages written to fd 3 by a userspace process onto
 * desktop windows. The bridge is LVGL-backed (this kernel's compositor):
 * each app window gets an lvgl_wm window whose content is an lv_canvas
 * backed by a software ARGB buffer (BGRA byte order, matching
 * LV_COLOR_DEPTH 32 TRUE_COLOR). Drawing happens on the app's thread with
 * interrupts masked; LVGL object work happens lazily on the compositor
 * thread through user_wm_tick(), so no LVGL call ever runs off-thread.
 *
 * Event flow (fd 4): CONN (window created), then KEY / MOUSE while the
 * window is active, CLOSED when the window is destroyed. App exits drop
 * their client on the next tick.
 */

#include "user_wm.h"
#include "kprintf.h"
#include "string.h"
#include "process.h"
#include "pmm.h"
#include "mm.h"
#include "fb.h"
#include "lvgl_wm.h"
#include "lvgl_port.h"
#include "lvgl/lvgl.h"

#include "../arch/arm64/font8x16.h"

/* ─── WM protocol constants (userspace/include/wm_protocol.h) ─── */
enum {
    WM_CREATE_WIN = 0x01,
    WM_CLOSE_WIN  = 0x02,
    WM_FILL_RECT  = 0x10,
    WM_DRAW_STR   = 0x11,
    WM_CLEAR      = 0x12,
    WM_FLUSH      = 0x15,

    WM_EV_KEY    = 0x80,
    WM_EV_MOUSE  = 0x81,
    WM_EV_CLOSED = 0x83,
    WM_EV_CONN   = 0x84,
};

#define UW_CANVAS_MAX ((size_t)0x100000) /* 1 MiB software canvas cap */

typedef struct {
    int used;
    int pid;
    int win;                       /* handle (slot index + 1) or 0 */
    uint8_t ev[UW_EV_RING][8];
    uint8_t ev_len[UW_EV_RING];
    int ev_head;
    int ev_tail;
} uw_client_t;

typedef struct {
    int used;
    int pid;
    int handle;                    /* wire handle (slot + 1) */
    int w, h;
    int x, y;
    uint32_t *buf;                 /* software ARGB surface (virtual) */
    uint64_t buf_phys;             /* physical base of the surface pages */
    size_t buf_pages;
    int dirty;                     /* needs LVGL repaint */
    int want_create;               /* build LVGL objects on next tick */
    int want_destroy;
    int have_canvas;               /* lvgl window created on compositor thread */
    int lw;                        /* lvgl_wm window id */
    lv_obj_t *canvas;
    char title[UW_MAX_TITLE + 1];
} uw_win_t;

static uw_client_t uw_clients[UW_MAX_CLIENTS];
static uw_win_t    uw_windows[UW_MAX_WINDOWS];
static int uw_ready;
static int uw_inited;

static lvgl_wm_t uw_wm;
static int uw_wm_init_done;

static int uw_last_click_win;      /* uw window handle (0 = none), for key routing */

/* Interrupt-masked critical section (single-CPU assumption, same approach
 * as the rest of the kernel's shared rings). */
static inline void uw_lock(void)   { __asm__ volatile("cli" : : : "memory"); }
static inline void uw_unlock(void) { __asm__ volatile("sti" : : : "memory"); }

/* ─── lookup helpers ─── */

static uw_client_t *uw_client_find(int pid) {
    for (int i = 0; i < UW_MAX_CLIENTS; i++)
        if (uw_clients[i].used && uw_clients[i].pid == pid)
            return &uw_clients[i];
    return 0;
}

static uw_client_t *uw_client_alloc(int pid) {
    uw_client_t *c;
    for (int i = 0; i < UW_MAX_CLIENTS; i++) {
        if (uw_clients[i].used) continue;
        c = &uw_clients[i];
        memset(c, 0, sizeof(*c));
        c->used = 1;
        c->pid = pid;
        return c;
    }
    return 0;
}

static void uw_client_free(uw_client_t *c) {
    memset(c, 0, sizeof(*c));
}

static uw_win_t *uw_win_by_handle(int handle) {
    int idx = handle - 1;
    if (idx < 0 || idx >= UW_MAX_WINDOWS) return 0;
    if (!uw_windows[idx].used) return 0;
    return &uw_windows[idx];
}

static void uw_queue_event(uw_client_t *c, const uint8_t *ev, int len) {
    if (!c || len <= 0 || len > 8) return;
    int next = (c->ev_tail + 1) % UW_EV_RING;
    if (next == c->ev_head) return; /* ring full: drop */
    memcpy(c->ev[c->ev_tail], ev, len);
    c->ev_len[c->ev_tail] = (uint8_t)len;
    c->ev_tail = next;
}

/* ─── software surface rendering ─── */

static void uw_surface_free(uw_win_t *w) {
    if (w->buf) {
        pmm_free_pages(w->buf_phys, w->buf_pages);
        w->buf = 0;
        w->buf_phys = 0;
        w->buf_pages = 0;
    }
}

static int uw_surface_alloc(uw_win_t *w) {
    size_t bytes = (size_t)w->w * (size_t)w->h * 4;
    if (bytes == 0 || bytes > UW_CANVAS_MAX) return -1;
    size_t pages = (bytes + 0xFFF) / 0x1000;
    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) {
        kprintf("user_wm: surface alloc failed (%u x %u)\n", w->w, w->h);
        return -1;
    }
    w->buf = (uint32_t *)phys_to_virt(phys);
    w->buf_phys = phys;
    w->buf_pages = pages;
    return 0;
}

static void uw_fill_rect(uw_win_t *w, int x, int y, int rw, int rh, uint32_t argb) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (rw <= 0 || rh <= 0) return;
    if (x >= w->w || y >= w->h) return;
    if (x + rw > w->w) rw = w->w - x;
    if (y + rh > w->h) rh = w->h - y;
    for (int row = 0; row < rh; row++) {
        uint32_t *dst = w->buf + (size_t)(y + row) * w->w + x;
        for (int i = 0; i < rw; i++) dst[i] = argb;
    }
}

static void uw_draw_str(uw_win_t *w, int x, int y, uint32_t argb, const char *s) {
    if (!s) return;
    while (*s) {
        uint8_t ch = (uint8_t)*s++;
        const uint8_t *g = &font8x16[ch * 16];
        for (int row = 0; row < 16; row++) {
            uint8_t bits = g[row];
            if (!bits) continue;
            int gy = y + row;
            if (gy < 0 || gy >= w->h) continue;
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) continue;
                int gx = x + col;
                if (gx < 0 || gx >= w->w) continue;
                w->buf[(size_t)gy * w->w + gx] = argb;
            }
        }
        x += 8;
        if (x >= w->w + 8) break;
    }
}

/* ─── compositor side (desktop thread) ─── */

static void uw_ensure_wm(void) {
    if (uw_wm_init_done) return;
    uw_wm_init_done = 1;
    int w = fb_getwidth();
    int h = fb_getheight();
    if (w <= 0) w = 1280;
    if (h <= 0) h = 800;
    lvgl_wm_init(&uw_wm, w, h, 24);
    /* The bridge WM owns a full-screen wrapper above the desktop: make it
     * fully transparent and not clickable so it never covers Qt's windows
     * or steals desktop input -- only the app canvases inside it show. */
    if (uw_wm.screen) {
        lv_obj_set_style_bg_opa(uw_wm.screen, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(uw_wm.screen, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(uw_wm.screen, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void uw_compose_window(uw_win_t *w) {
    lvgl_wm_rect_t rect;
    int id = lvgl_wm_create_window(&uw_wm, w->title, w->w, w->h, &rect);
    if (!id) return;
    lvgl_wm_window_t *lw = lvgl_wm_find_window(&uw_wm, id);
    if (!lw || !lw->obj) {
        lvgl_wm_destroy_window(&uw_wm, id);
        return;
    }
    w->x = rect.x;
    w->y = rect.y;
    w->lw = id;

    lv_obj_t *cv = lv_canvas_create(lw->obj);
    if (!cv) {
        lvgl_wm_destroy_window(&uw_wm, id);
        return;
    }
    lv_canvas_set_buffer(cv, w->buf, w->w, w->h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(cv, 0, 0);
    lv_obj_set_size(cv, w->w, w->h);
    lv_obj_move_to_index(lw->obj, -1); /* float above the desktop */

    w->canvas = cv;
    w->have_canvas = 1;
    w->dirty = 1;
}

static void uw_destroy_window(uw_win_t *w) {
    uw_client_t *c = uw_client_find(w->pid);
    uint8_t ev[2];

    if (w->have_canvas) {
        if (w->canvas) lv_obj_del(w->canvas);
        if (w->lw) lvgl_wm_destroy_window(&uw_wm, w->lw);
        w->have_canvas = 0;
        w->canvas = 0;
        w->lw = 0;
    }
    if (uw_last_click_win == w->handle)
        uw_last_click_win = 0;

    ev[0] = WM_EV_CLOSED;
    ev[1] = (uint8_t)w->handle;
    uw_queue_event(c, ev, 2);

    uw_surface_free(w);
    memset(w, 0, sizeof(*w));

    if (c && c->win == w->handle)
        c->win = 0;
}

void user_wm_tick(void) {
    if (!uw_ready) return;
    if (!lvgl_port_ready()) return;

    uw_lock();
    uw_ensure_wm();

    /* create */
    for (int i = 0; i < UW_MAX_WINDOWS; i++) {
        uw_win_t *w = &uw_windows[i];
        if (w->used && w->want_create && !w->have_canvas) {
            uw_compose_window(w);
            if (w->have_canvas) {
                uw_client_t *c = uw_client_find(w->pid);
                uint8_t ev[2] = { WM_EV_CONN, (uint8_t)w->handle };
                uw_queue_event(c, ev, 2);
            }
            w->want_create = 0;
        }
    }

    /* repaint dirty surfaces */
    for (int i = 0; i < UW_MAX_WINDOWS; i++) {
        uw_win_t *w = &uw_windows[i];
        if (w->used && w->have_canvas && w->dirty && w->canvas) {
            lv_obj_invalidate(w->canvas);
            w->dirty = 0;
        }
    }

    /* destroy */
    for (int i = 0; i < UW_MAX_WINDOWS; i++) {
        uw_win_t *w = &uw_windows[i];
        if (w->used && w->want_destroy) {
            uw_destroy_window(w);
        }
    }

    /* reap dead clients (window close or process gone) */
    for (int i = 0; i < UW_MAX_CLIENTS; i++) {
        uw_client_t *c = &uw_clients[i];
        if (!c->used) continue;
        if (proc_get(c->pid) == 0 && c->win == 0) {
            uw_client_free(c);
            continue;
        }
        if (proc_get(c->pid) == 0) {
            /* process exited: close its windows, then drop the client */
            for (int j = 0; j < UW_MAX_WINDOWS; j++)
                if (uw_windows[j].used && uw_windows[j].pid == c->pid)
                    uw_windows[j].want_destroy = 1;
            uw_client_free(c);
        }
    }
    uw_unlock();
}

void user_wm_init(void) {
    uw_ready = 1;
    uw_inited = 1;
}

int user_wm_ready(void) {
    return uw_ready;
}

/* ─── input routing (compositor thread) ─── */

static int pid_of_handle(int handle) {
    uw_win_t *w = uw_win_by_handle(handle);
    return w ? w->pid : -1;
}

void user_wm_input_pointer(int x, int y, int pressed) {
    uw_client_t *c;
    uw_win_t *w;
    int hid;

    if (!uw_ready || !uw_wm_init_done) return;

    uw_lock();
    if (pressed) {
        int r = lvgl_wm_hit_test(&uw_wm, x, y);
        hid = 0;
        if (r) {
            lvgl_wm_window_t *lw = lvgl_wm_find_window(&uw_wm, r);
            if (lw) {
                for (int i = 0; i < UW_MAX_WINDOWS; i++)
                    if (uw_windows[i].used && uw_windows[i].lw == r)
                        hid = uw_windows[i].handle;
            }
        }
        if (hid) uw_last_click_win = hid;
    }

    hid = uw_last_click_win;
    if (hid && pid_of_handle(hid) >= 0) {
        w = uw_win_by_handle(hid);
        if (w && w->used) {
            c = uw_client_find(w->pid);
            if (c) {
                uint8_t ev[8];
                int wx = x - w->x;
                int wy = y - w->y;
                ev[0] = WM_EV_MOUSE;
                ev[1] = (uint8_t)hid;
                ev[2] = wx & 0xFF; ev[3] = (wx >> 8) & 0xFF;
                ev[4] = wy & 0xFF; ev[5] = (wy >> 8) & 0xFF;
                ev[6] = pressed & 0xFF; ev[7] = 0;
                uw_queue_event(c, ev, 8);
            }
        }
    }
    uw_unlock();
}

void user_wm_input_key(uint8_t key, int pressed) {
    uw_client_t *c;
    uw_win_t *w;

    if (!uw_ready) return;
    uw_lock();
    w = uw_win_by_handle(uw_last_click_win);
    if (w && w->used) {
        c = uw_client_find(w->pid);
        if (c) {
            uint8_t ev[6];
            uint32_t k = key;
            ev[0] = WM_EV_KEY;
            ev[1] = (uint8_t)w->handle;
            ev[2] = k & 0xFF; ev[3] = (k >> 8) & 0xFF;
            ev[4] = (k >> 16) & 0xFF; ev[5] = (k >> 24) & 0xFF;
            if (pressed) uw_queue_event(c, ev, 6);
        }
    }
    uw_unlock();
}

/* ─── syscall side ─── */

int user_wm_setup(int pid) {
    if (!uw_ready) return -1;
    uw_lock();
    if (!uw_client_find(pid)) {
        if (!uw_client_alloc(pid)) {
            uw_unlock();
            return -1;
        }
    }
    uw_unlock();
    return 0;
}

int user_wm_active(int pid) {
    return uw_client_find(pid) != 0;
}

int user_wm_release(int pid) {
    uw_lock();
    uw_client_t *c = uw_client_find(pid);
    if (c) uw_client_free(c);
    for (int i = 0; i < UW_MAX_WINDOWS; i++)
        if (uw_windows[i].used && uw_windows[i].pid == pid)
            uw_windows[i].want_destroy = 1;
    uw_unlock();
    return 0;
}

int user_wm_msg_in(int pid, const void *buf, int len) {
    const uint8_t *m = buf;
    uw_client_t *c;
    uw_win_t *w;
    int r;

    if (!uw_ready || !buf || len < 1) return -1;
    uw_lock();
    c = uw_client_find(pid);
    if (!c) { uw_unlock(); return -1; }

    switch (m[0]) {
    case WM_CREATE_WIN: {
        if (len < 6) { uw_unlock(); return -1; }
        int ww = m[1] | (m[2] << 8);
        int wh = m[3] | (m[4] << 8);
        int tlen = m[5];
        if (ww < 8) ww = 8;
        if (wh < 8) wh = 8;
        if (c->win) { /* apps use a single window */
            uw_unlock();
            return 0;
        }
        /* find a free slot */
        int slot = -1;
        for (int i = 0; i < UW_MAX_WINDOWS; i++) {
            if (!uw_windows[i].used) { slot = i; break; }
        }
        if (slot < 0) { uw_unlock(); return -1; }
        w = &uw_windows[slot];
        memset(w, 0, sizeof(*w));
        w->used = 1;
        w->pid = pid;
        w->handle = slot + 1;
        w->w = ww;
        w->h = wh;
        w->want_create = 1;
        if (tlen > 0) {
            if (tlen > UW_MAX_TITLE) tlen = UW_MAX_TITLE;
            for (int i = 0; i < tlen && 6 + i < len; i++)
                w->title[i] = m[6 + i];
        }
        if (uw_surface_alloc(w) < 0) {
            memset(w, 0, sizeof(*w));
            uw_unlock();
            return -1;
        }
        c->win = w->handle;
        uw_unlock();
        return 0;
    }
    case WM_CLOSE_WIN: {
        if (len < 2) { uw_unlock(); return -1; }
        w = uw_win_by_handle(m[1]);
        if (w && w->pid == pid) w->want_destroy = 1;
        uw_unlock();
        return 0;
    }
    default:
        break;
    }

    if (len < 2) { uw_unlock(); return -1; }
    w = uw_win_by_handle(m[1]);
    if (!w || w->pid != pid) { uw_unlock(); return -1; }

    switch (m[0]) {
    case WM_FILL_RECT: {
        if (len < 14) break;
        int x = m[2] | (m[3] << 8);
        int y = m[4] | (m[5] << 8);
        int rw = m[6] | (m[7] << 8);
        int rh = m[8] | (m[9] << 8);
        uint32_t col = m[10] | (m[11] << 8) | (m[12] << 16) | ((uint32_t)m[13] << 24);
        uw_fill_rect(w, x, y, rw, rh, col);
        break;
    }
    case WM_DRAW_STR: {
        if (len < 11) break;
        int x = m[2] | (m[3] << 8);
        int y = m[4] | (m[5] << 8);
        uint32_t col = m[6] | (m[7] << 8) | (m[8] << 16) | ((uint32_t)m[9] << 24);
        int slen = m[10];
        if (slen > len - 11) slen = len - 11;
        char tmp[256];
        if (slen > 255) slen = 255;
        memcpy(tmp, m + 11, slen);
        tmp[slen] = 0;
        uw_draw_str(w, x, y, col, tmp);
        break;
    }
    case WM_CLEAR: {
        uw_fill_rect(w, 0, 0, w->w, w->h, 0xFF000000);
        break;
    }
    case WM_FLUSH: {
        w->dirty = 1;
        break;
    }
    default:
        r = -1;
        uw_unlock();
        return r;
    }
    uw_unlock();
    return 0;
}

int user_wm_events_out(int pid, void *buf, int max) {
    uw_client_t *c;
    uint8_t *out = buf;
    int n = 0;

    if (!uw_ready || !buf || max <= 0) return 0;
    uw_lock();
    c = uw_client_find(pid);
    if (!c) { uw_unlock(); return 0; }
    while (c->ev_head != c->ev_tail && n < max) {
        int len = c->ev_len[c->ev_head];
        if (len > max - n) break;
        memcpy(out + n, c->ev[c->ev_head], len);
        n += len;
        c->ev_head = (c->ev_head + 1) % UW_EV_RING;
    }
    uw_unlock();
    return n;
}
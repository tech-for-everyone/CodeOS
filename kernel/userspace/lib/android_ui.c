#include "android_ui.h"

#include "stdio.h"

/* ── wire helpers (mirror pkgs/core/terminal/src/terminal.c) ── */

static void au_send(const void *msg, int len) {
    sys_pwrite(WM_PIPE_CMD, msg, len);
}

static void au_msg_create_win(int w, int h, const char *title) {
    uint8_t msg[64];
    int p = 0;
    msg[p++] = WM_CREATE_WIN;
    msg[p++] = w & 0xFF; msg[p++] = (w >> 8) & 0xFF;
    msg[p++] = h & 0xFF; msg[p++] = (h >> 8) & 0xFF;
    msg[p++] = (uint8_t)strlen(title);
    while (*title) msg[p++] = (uint8_t)*title++;
    au_send(msg, p);
}

static void au_msg_close(int win) {
    uint8_t msg[2] = { WM_CLOSE_WIN, (uint8_t)win };
    au_send(msg, 2);
}

static void au_msg_fill_rect(int win, int x, int y, int w, int h, uint32_t color) {
    uint8_t msg[16];
    msg[0] = WM_FILL_RECT;
    msg[1] = (uint8_t)win;
    msg[2] = x & 0xFF; msg[3] = (x >> 8) & 0xFF;
    msg[4] = y & 0xFF; msg[5] = (y >> 8) & 0xFF;
    msg[6] = w & 0xFF; msg[7] = (w >> 8) & 0xFF;
    msg[8] = h & 0xFF; msg[9] = (h >> 8) & 0xFF;
    msg[10] = color & 0xFF; msg[11] = (color >> 8) & 0xFF;
    msg[12] = (color >> 16) & 0xFF; msg[13] = (color >> 24) & 0xFF;
    au_send(msg, 14);
}

static void au_msg_draw_str(int win, int x, int y, uint32_t color, const char *s) {
    int slen = (int)strlen(s);
    if (slen > 254) slen = 254;
    uint8_t msg[270];
    int p = 0;
    msg[p++] = WM_DRAW_STR;
    msg[p++] = (uint8_t)win;
    msg[p++] = x & 0xFF; msg[p++] = (x >> 8) & 0xFF;
    msg[p++] = y & 0xFF; msg[p++] = (y >> 8) & 0xFF;
    msg[p++] = color & 0xFF; msg[p++] = (color >> 8) & 0xFF;
    msg[p++] = (color >> 16) & 0xFF; msg[p++] = (color >> 24) & 0xFF;
    msg[p++] = (uint8_t)slen;
    for (int i = 0; i < slen; i++) msg[p++] = (uint8_t)s[i];
    au_send(msg, p);
}

static void au_msg_outline(int win, int x, int y, int w, int h, uint32_t color) {
    au_msg_fill_rect(win, x,     y,     w, 1, color); /* top    */
    au_msg_fill_rect(win, x,     y + h - 1, w, 1, color); /* bottom */
    au_msg_fill_rect(win, x,     y, 1, h, color); /* left   */
    au_msg_fill_rect(win, x + w - 1, y, 1, h, color); /* right  */
}

static void au_msg_clear(int win) {
    uint8_t msg[2] = { WM_CLEAR, (uint8_t)win };
    au_send(msg, 2);
}

static void au_msg_flush(int win) {
    uint8_t msg[2] = { WM_FLUSH, (uint8_t)win };
    au_send(msg, 2);
}

/* ── time ── */

int au_ms(au_app_t *a) {
    timeval_t tv;
    memset(&tv, 0, sizeof(tv));
    if (sys_gettimeofday(&tv) == 0) {
        int64_t now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        return (int)(now - a->t0_us);
    }
    return (int)(a->frame * 33);
}

/* ── lifecycle ── */

au_app_t *au_start(const char *title, int w, int h) {
    static au_app_t app;
    char line[128];
    int p = 0;
    const char *tag = "android-ui: ";

    memset(&app, 0, sizeof(app));
    app.width = w;
    app.height = h;
    app.win = 0;

    {
        timeval_t tv;
        memset(&tv, 0, sizeof(tv));
        if (sys_gettimeofday(&tv) == 0)
            app.t0_us = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    /* Hosted by the async apphost (Qt dock): stdin is a terminal, there is no
     * WM pump behind fds 3/4, so skip the bridge handshake entirely and run
     * in console mode from the start. */
    if (sys_get_info(INFO_APPHOST, NULL, 0) == 1) {
        for (int i = 0; tag[i]; i++) line[p++] = tag[i];
        for (int i = 0; title[i] && p < 126; i++) line[p++] = title[i];
        line[p] = 0;
        putchar('\n');
        puts(line);
        puts("  hosted by apphost - running in console mode");
        printf("  canvas %dx%d, frame loop active until 'exit' or timeout\n", w, h);
        app.wm = AU_NO_WM;
        return &app;
    }

    /* Try to open a window through the WM bridge on fd 3. */
    au_msg_create_win(w, h, title);
    int have_wm = 0;
    for (int tries = 0; tries < 60 && !have_wm; tries++) {
        uint8_t buf[2];
        int n = sys_read(WM_PIPE_EVENT, buf, 2);
        if (n >= 2 && buf[0] == WM_EVENT_CONN) {
            app.win = buf[1];
            have_wm = 1;
            break;
        }
        sys_sleep(10);
    }

    if (have_wm) {
        app.wm = AU_WM_OK;
        return &app;
    }

    printf("android-ui: %s - window bridge unavailable, console mode\n", title);
    app.wm = AU_NO_WM;
    for (int i = 0; tag[i]; i++) line[p++] = tag[i];
    for (int i = 0; title[i] && p < 126; i++) line[p++] = title[i];
    line[p] = 0;
    putchar('\n');
    puts(line);
    puts("  window bridge unavailable - running in console mode");
    printf("  canvas %dx%d, frame loop active until 'exit' or timeout\n", w, h);
    return &app;
}

void au_end(au_app_t *a) {
    if (a && a->wm == AU_WM_OK)
        au_msg_close(a->win);
    if (a)
        puts("android-ui: app finished");
}

/* ── drawing ── */

void au_clear(au_app_t *a) {
    if (!a || a->wm != AU_WM_OK) return;
    au_msg_clear(a->win);
}

void au_rect(au_app_t *a, int x, int y, int w, int h, uint32_t col) {
    if (!a || a->wm != AU_WM_OK) return;
    if (w <= 0 || h <= 0) return;
    if (x + w > a->width + 4) w = a->width + 4 - x;
    if (y + h > a->height + 4) h = a->height + 4 - y;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    au_msg_fill_rect(a->win, x, y, w, h, col);
}

void au_outline(au_app_t *a, int x, int y, int w, int h, uint32_t col) {
    if (!a || a->wm != AU_WM_OK) return;
    au_msg_outline(a->win, x, y, w, h, col);
}

void au_text(au_app_t *a, int x, int y, uint32_t col, const char *s) {
    if (!a || a->wm != AU_WM_OK || !s) return;
    au_msg_draw_str(a->win, x, y, col, s);
}

void au_flush(au_app_t *a) {
    if (!a || a->wm != AU_WM_OK) return;
    au_msg_flush(a->win);
}

/* ── widgets ── */

int au_button(au_app_t *a, int x, int y, int w, int h,
              const char *label, uint32_t bg, uint32_t fg) {
    if (!a) return 0;
    if (a->wm == AU_WM_OK) {
        au_msg_fill_rect(a->win, x, y, w, h, bg);
        int tw = 0;
        while (label && label[tw]) tw++;
        int tx = x + (w - tw * 6) / 2;
        if (tx < x + 2) tx = x + 2;
        au_msg_draw_str(a->win, tx, y + (h - 12) / 2, fg, label);
    }
    return 1;
}

void au_label(au_app_t *a, int x, int y, int w, const char *label,
              uint32_t bg, uint32_t fg) {
    if (!a) return;
    if (a->wm == AU_WM_OK) {
        au_msg_fill_rect(a->win, x, y, w, 20, bg);
        au_msg_draw_str(a->win, x + 4, y + 4, fg, label);
    } else {
        printf("  %s\n", label ? label : "");
    }
}

void au_statusbar(au_app_t *a, const char *title, int hh, int mm) {
    if (!a) return;
    if (a->wm == AU_WM_OK) {
        char t[64];
        int p = 0;
        if (hh < 0 || mm < 0) {
            for (int i = 0; title && title[i] && p < 30; i++) t[p++] = title[i];
            t[p] = 0;
        } else {
            snprintf(t, sizeof(t), "%02d:%02d  -  %s", hh, mm, title ? title : "");
        }
        au_msg_fill_rect(a->win, 0, 0, a->width, AU_STATUS_H, AU_TEXT);
        au_msg_draw_str(a->win, 6, 6, 0xFFFFFFFF, t);
    } else {
        printf("  [status] %s %02d:%02d\n", title ? title : "", hh, mm);
    }
}

/* ── events ── */

int au_poll(au_app_t *a, int *key_out, int *mx, int *my, int *mbuttons) {
    if (!a) return 0;
    if (a->wm != AU_WM_OK) {
        /* Console mode: no upstream events; apps drive on their own timer. */
        return 0;
    }
    uint8_t ev[8];
    memset(ev, 0, sizeof(ev));
    int n = sys_read(WM_PIPE_EVENT, ev, sizeof(ev));
    if (n <= 0) return 0;

    switch (ev[0]) {
    case WM_EVENT_KEY:
        if (key_out) {
            *key_out = (int)ev[2] | ((int)ev[3] << 8) |
                       ((int)ev[4] << 16) | ((int)ev[5] << 24);
        }
        return WM_EVENT_KEY;
    case WM_EVENT_MOUSE:
        if (mx) *mx = (int)ev[2] | ((int)ev[3] << 8);
        if (my) *my = (int)ev[4] | ((int)ev[5] << 8);
        if (mbuttons) *mbuttons = (int)ev[6] | ((int)ev[7] << 8);
        return WM_EVENT_MOUSE;
    case WM_EVENT_FOCUS:
        return WM_EVENT_FOCUS;
    case WM_EVENT_CLOSED:
        a->closed = 1;
        return WM_EVENT_CLOSED;
    default:
        return ev[0];
    }
}

/* ── console fallback ── */

void au_console_line(const char *s) {
    if (!s) return;
    printf("android: %s\n", s);
}

int au_console_mode(const au_app_t *a) {
    return !a || a->wm != AU_WM_OK;
}

const char *au_version(void) {
    return "android-ui 1.0 (codeos)";
}
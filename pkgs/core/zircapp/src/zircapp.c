/* ─────────────────────────────────────────────────────────────
 * zircapp — the CodeOS windowed host for the ZircApp framework.
 *
 * Phase 2 of the Zircon→ZircApp port: the whole Zircon application
 * layer (Zircon/zircapp + Zircon/apps) is linked into this single,
 * ordinary CodeOS userspace program.  No container and no kernel
 * Zircon: the host arms the kernel user-window bridge, opens one
 * desktop window, and drives the framework itself.
 *
 *   - `sys_wm_attach()` arms the WM bridge (fds 3/4) for this pid.
 *   - A single WM window is the "ZircApp screen".  Zircon's
 *     `gui_window_create()` is a lightweight recorder in the port
 *     (gui/windows.c), so the ten apps that call it do *not* each
 *     open a WM window.
 *   - The host paints a Home screen that enumerates the registry
 *     (`zircapp_get(i)->name`) and renders it with WM_FILL_RECT /
 *     WM_DRAW_STR / WM_FLUSH.
 *   - Clicking a cell calls `zircapp_launch(name)` and broadcasts a
 *     touch event to every registered app, which is the framework's
 *     own event semantics.
 *
 * When the bridge is unavailable (headless boot, or `--selftest`) the
 * host runs a bounded console session instead and exits, so a serial
 * boot can verify the framework without a compositor.
 * ───────────────────────────────────────────────────────────── */
#include "zircapp.h"
#include "unistd.h"
#include "wm_protocol.h"

#include <string.h>
#include <stdio.h>

/* The launcher.  Registering it also registers the nine phone apps
 * (Zircon/apps/home.c: home_init calls zircapp_register for each). */
extern zircapp_app_t home_app;

/* Provided by pkgs/core/zircapp/src/zircapp_platform.c (and declared the
 * same way in Zircon/zircapp/zircapp.c).  freestd unistd.h has no usleep. */
extern int usleep(unsigned int usec);

/* ── Logical screen (the ZircApp window).  Must stay under the kernel
 * bridge's 1 MiB software-canvas cap: W*H*4 <= 0x100000. ── */
#define SCREEN_W 400
#define SCREEN_H 600

#define HEADER_H  44
#define FOOTER_H  28
#define GRID_MRG  12
#define CELL_GAP  10
#define CELL_H    64
#define COLS      2

/* Catppuccin-ish palette, ARGB. */
#define COL_BAR     0xFF1E1E2E
#define COL_BG      0xFF181825
#define COL_CELL    0xFF24243A
#define COL_CELL_HI 0xFF313244
#define COL_ACCENT  0xFF89B4FA
#define COL_TEXT    0xFFCDD6F4
#define COL_DIM     0xFFA6ADC8
#define COL_FAINT   0xFF6C7086
#define COL_FOOT    0xFF11111B

static int win_handle = -1;
static int have_window;

enum { MODE_HOME = 0, MODE_APP = 1 };
static int mode = MODE_HOME;
static int active_app = -1;

/* ── WM wire helpers (layouts mirror kernel/kernel/user_wm.c and
 * kernel/userspace/lib/android_ui.c). ── */

static void wm_send(const void *msg, int len) {
    sys_pwrite(WM_PIPE_CMD, msg, len);
}

static void wm_create(int w, int h, const char *title) {
    uint8_t msg[64];
    int p = 0;
    msg[p++] = WM_CREATE_WIN;
    msg[p++] = (uint8_t)(w & 0xFF);
    msg[p++] = (uint8_t)((w >> 8) & 0xFF);
    msg[p++] = (uint8_t)(h & 0xFF);
    msg[p++] = (uint8_t)((h >> 8) & 0xFF);
    msg[p++] = (uint8_t)strlen(title);
    while (*title) msg[p++] = (uint8_t)*title++;
    wm_send(msg, p);
}

static void wm_fill(int x, int y, int w, int h, uint32_t color) {
    if (win_handle < 0) return;
    uint8_t msg[14];
    msg[0] = WM_FILL_RECT;
    msg[1] = (uint8_t)win_handle;
    msg[2] = (uint8_t)(x & 0xFF);      msg[3] = (uint8_t)((x >> 8) & 0xFF);
    msg[4] = (uint8_t)(y & 0xFF);      msg[5] = (uint8_t)((y >> 8) & 0xFF);
    msg[6] = (uint8_t)(w & 0xFF);      msg[7] = (uint8_t)((w >> 8) & 0xFF);
    msg[8] = (uint8_t)(h & 0xFF);      msg[9] = (uint8_t)((h >> 8) & 0xFF);
    msg[10] = (uint8_t)(color & 0xFF);
    msg[11] = (uint8_t)((color >> 8) & 0xFF);
    msg[12] = (uint8_t)((color >> 16) & 0xFF);
    msg[13] = (uint8_t)((color >> 24) & 0xFF);
    wm_send(msg, 14);
}

static void wm_text(int x, int y, uint32_t color, const char *s) {
    if (win_handle < 0 || !s) return;
    int slen = (int)strlen(s);
    if (slen > 200) slen = 200;
    uint8_t msg[212];
    int p = 0;
    msg[p++] = WM_DRAW_STR;
    msg[p++] = (uint8_t)win_handle;
    msg[p++] = (uint8_t)(x & 0xFF);    msg[p++] = (uint8_t)((x >> 8) & 0xFF);
    msg[p++] = (uint8_t)(y & 0xFF);    msg[p++] = (uint8_t)((y >> 8) & 0xFF);
    msg[p++] = (uint8_t)(color & 0xFF);
    msg[p++] = (uint8_t)((color >> 8) & 0xFF);
    msg[p++] = (uint8_t)((color >> 16) & 0xFF);
    msg[p++] = (uint8_t)((color >> 24) & 0xFF);
    msg[p++] = (uint8_t)slen;
    for (int i = 0; i < slen; i++) msg[p++] = (uint8_t)s[i];
    wm_send(msg, p);
}

static void wm_flush(void) {
    if (win_handle < 0) return;
    uint8_t msg[2] = { WM_FLUSH, (uint8_t)win_handle };
    wm_send(msg, 2);
}

static void wm_close(void) {
    if (win_handle < 0) return;
    uint8_t msg[2] = { WM_CLOSE_WIN, (uint8_t)win_handle };
    wm_send(msg, 2);
    win_handle = -1;
}

/* ── Layout + painting ── */

static void cell_rect(int idx, int *x, int *y, int *w, int *h) {
    int cw = (SCREEN_W - GRID_MRG * (COLS + 1)) / COLS;
    *x = GRID_MRG + (idx % COLS) * (cw + GRID_MRG);
    *y = HEADER_H + GRID_MRG + (idx / COLS) * (CELL_H + CELL_GAP);
    *w = cw;
    *h = CELL_H;
}

static void draw_status(void) {
    zircapp_battery_t b = zircapp_battery_get();
    zircapp_network_t n = zircapp_network_get();
    char buf[80];
    snprintf(buf, sizeof(buf), "battery %d%%  net %d  wifi %d",
             b.level, n.network_type, n.wifi_connected);
    wm_fill(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, COL_FOOT);
    wm_text(8, SCREEN_H - FOOTER_H + (FOOTER_H - 16) / 2, COL_DIM, buf);
}

static void draw_home(void) {
    char buf[48];
    int n = zircapp_count();

    wm_fill(0, 0, SCREEN_W, HEADER_H, COL_BAR);
    wm_text(12, (HEADER_H - 16) / 2, COL_TEXT, "ZircApp");
    snprintf(buf, sizeof(buf), "%d apps", n);
    wm_text(SCREEN_W - 12 - (int)strlen(buf) * 8, (HEADER_H - 16) / 2,
            COL_ACCENT, buf);

    wm_fill(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H - FOOTER_H, COL_BG);
    for (int i = 0; i < zircapp_count(); i++) {
        zircapp_app_t *a = zircapp_get(i);
        int x, y, w, h;
        if (!a) continue;
        cell_rect(i, &x, &y, &w, &h);
        wm_fill(x, y, w, h, (i == active_app) ? COL_CELL_HI : COL_CELL);
        wm_fill(x, y, w, 2, COL_ACCENT);
        wm_text(x + 10, y + (CELL_H - 16) / 2, COL_TEXT, a->name);
    }
    draw_status();
    wm_flush();
}

static void draw_app_screen(void) {
    zircapp_app_t *a = zircapp_get(active_app);
    if (!a) { mode = MODE_HOME; active_app = -1; draw_home(); return; }

    wm_fill(0, 0, SCREEN_W, HEADER_H, COL_BAR);
    wm_text(12, (HEADER_H - 16) / 2, COL_ACCENT, "< Home");
    wm_text(96, (HEADER_H - 16) / 2, COL_TEXT, a->name);

    wm_fill(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H - FOOTER_H, COL_BG);
    wm_text(16, HEADER_H + 22, COL_TEXT,  "ZircApp app running under CodeOS");
    wm_text(16, HEADER_H + 46, COL_DIM,   "touch events broadcast to all apps");

    char buf[64];
    snprintf(buf, sizeof(buf), "registry index %d", active_app);
    wm_text(16, HEADER_H + 76, COL_FAINT, buf);
    snprintf(buf, sizeof(buf), "gui window id %d", zircapp_get(active_app)->win_id);
    wm_text(16, HEADER_H + 100, COL_FAINT, buf);

    draw_status();
    wm_flush();
}

/* ── Events ── */

static void broadcast_touch(int x, int y, zircapp_event_type_t type) {
    zircapp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.mx = x;
    ev.my = y;
    ev.win_id = win_handle;
    ev.touch_count = 1;
    ev.touch[0].id = 0;
    ev.touch[0].x = x;
    ev.touch[0].y = y;
    ev.touch[0].pressure = 1;
    zircapp_broadcast(&ev);
}

static int hit_cell(int px, int py) {
    int n = zircapp_count();
    for (int i = 0; i < n; i++) {
        int x, y, w, h;
        cell_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h)
            return i;
    }
    return -1;
}

static void goto_home(void) {
    mode = MODE_HOME;
    active_app = -1;
    draw_home();
}

static void do_launch(int idx) {
    zircapp_app_t *a = zircapp_get(idx);
    if (!a) return;
    active_app = idx;
    mode = MODE_APP;
    zircapp_launch(a->name);
    printf("zircapp: launch '%s' (index %d)\n", a->name, idx);
    draw_app_screen();
}

static void on_mouse(int x, int y, int pressed) {
    if (pressed) {
        broadcast_touch(x, y, ZIRCAPP_EVENT_TOUCH_DOWN);
        if (mode == MODE_HOME) {
            int idx = hit_cell(x, y);
            if (idx >= 0) do_launch(idx);
        } else if (x < 96 && y < HEADER_H) {
            goto_home();
        }
    } else {
        broadcast_touch(x, y, ZIRCAPP_EVENT_TOUCH_UP);
    }
}

static void on_key(int key) {
    zircapp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ZIRCAPP_EVENT_KEY_DOWN;
    ev.key = key;
    ev.win_id = win_handle;
    zircapp_broadcast(&ev);
    if (key == 27) { /* Esc: back, or close when already home */
        if (mode == MODE_APP) {
            mode = MODE_HOME;
            active_app = -1;
            draw_home();
        } else {
            have_window = 0;
        }
    }
}

static void handle_event(const uint8_t *p) {
    switch (p[0]) {
    case WM_EVENT_CONN:
        win_handle = p[1];
        have_window = 1;
        break;
    case WM_EVENT_CLOSED:
        have_window = 0;
        break;
    case WM_EVENT_KEY: {
        uint32_t k = (uint32_t)p[2] | ((uint32_t)p[3] << 8) |
                     ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
        on_key((int)k);
        break;
    }
    case WM_EVENT_MOUSE: {
        int x = (int)(int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
        int y = (int)(int16_t)((uint16_t)p[4] | ((uint16_t)p[5] << 8));
        on_mouse(x, y, p[6] != 0);
        break;
    }
    default:
        break;
    }
}

/* ── Drain everything the bridge has queued for us. ──
 * The event ring is a byte stream of fixed-size records, so the record
 * boundaries are recovered from the opcode, exactly as the kernel
 * queues them (user_wm.c). */
static void pump_events(void) {
    uint8_t buf[128];
    int n = sys_read(WM_PIPE_EVENT, buf, (int)sizeof(buf));
    int off = 0;
    while (off < n) {
        int left = n - off;
        int len = 0;
        switch (buf[off]) {
            case WM_EVENT_CONN:
            case WM_EVENT_CLOSED: len = 2; break;
            case WM_EVENT_KEY:    len = 6; break;
            case WM_EVENT_MOUSE:  len = 8; break;
            default:              off = n; continue;
        }
        if (len > left) break;
        handle_event(buf + off);
        off += len;
    }
}

/* ── Frame loop (windowed) ── */

static int run_windowed(void) {
    wm_create(SCREEN_W, SCREEN_H, "ZircApp");

    /* Wait for the compositor to build our canvas and send CONN. */
    for (int waited = 0; waited < 2000 && !have_window; waited += 10) {
        pump_events();
        if (!have_window) usleep(10000);
    }
    if (!have_window) {
        printf("zircapp: no compositor response\n");
        return 0;
    }

    printf("zircapp: window ready handle=%d %dx%d\n",
           win_handle, SCREEN_W, SCREEN_H);
    mode = MODE_HOME;
    active_app = -1;
    draw_home();

    int tick_acc = 0;
    while (have_window) {
        pump_events();
        usleep(16000); /* ~60 Hz, as zircapp_run() does */
        tick_acc += 16;
        if (tick_acc >= 500) { /* ~2 Hz world tick; apps redraw themselves */
            tick_acc = 0;
            zircapp_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = ZIRCAPP_EVENT_TICK;
            zircapp_broadcast(&ev);
        }
    }
    wm_close();
    printf("zircapp: window closed\n");
    return 1;
}

/* ── Console session (no compositor, or --selftest) ── */

static const char *kSubApps[] = {
    "Phone", "SMS", "Settings", "Camera", "Clock",
    "Calculator", "Music", "Gallery", "Browser",
};

static int run_console(int selftest) {
    printf("zircapp: console mode\n");
    for (int i = 0; i < zircapp_count(); i++) {
        zircapp_app_t *a = zircapp_get(i);
        if (a) printf("zircapp: app[%d] = '%s'\n", i, a->name);
    }
    if (!selftest)
        return 1;

    /* Exercise the framework end-to-end without a window: launch each
     * app, then broadcast the same events the host would. */
    int launched = 0;
    for (unsigned i = 0; i < sizeof(kSubApps) / sizeof(kSubApps[0]); i++) {
        if (zircapp_launch(kSubApps[i]) >= 0) launched++;
        else printf("zircapp: launch failed '%s'\n", kSubApps[i]);
    }
    printf("zircapp: selftest launch %d/%d\n", launched,
           (int)(sizeof(kSubApps) / sizeof(kSubApps[0])));

    zircapp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ZIRCAPP_EVENT_TOUCH_DOWN;
    ev.touch_count = 1;
    ev.touch[0].pressure = 1;
    zircapp_broadcast(&ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = ZIRCAPP_EVENT_KEY_DOWN;
    ev.key = 'a';
    zircapp_broadcast(&ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = ZIRCAPP_EVENT_TICK;
    zircapp_broadcast(&ev);

    printf("zircapp: selftest apps=%d launched=%d\n", zircapp_count(), launched);

    /* Home is itself a launchable registry entry, and zircapp_launch()
     * re-runs init().  Relaunching it must not append a second copy of
     * its nine children (home.c guards with apps_registered). */
    int reg_before = zircapp_count();
    zircapp_launch("Home");
    zircapp_launch("Home");
    int reg_after = zircapp_count();
    printf("zircapp: relaunch Home registry %d -> %d\n",
           reg_before, reg_after);

    return launched == (int)(sizeof(kSubApps) / sizeof(kSubApps[0])) &&
           reg_after == reg_before;
}

int main(int argc, char **argv) {
    int selftest = 0;
    int force_console = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--selftest") == 0 ||
            strcmp(argv[i], "--list") == 0) {
            selftest = 1;
            force_console = 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            force_console = 1;
        }
    }

    printf("zircapp: Zircon application framework on CodeOS\n");

    zircapp_init();
    zircapp_phone_init();
    zircapp_phone_set_screen(SCREEN_W, SCREEN_H);
    zircapp_register(&home_app);
    printf("zircapp: framework up (%d apps registered)\n", zircapp_count());

    if (!force_console && sys_wm_attach() >= 0 && run_windowed())
        return 0;

    /* No window: run a bounded console session so a headless boot still
     * proves the framework and the apps load and dispatch. */
    int ok = run_console(selftest);
    printf("zircapp: done status=%d\n", ok ? 0 : 1);
    return ok ? 0 : 1;
}

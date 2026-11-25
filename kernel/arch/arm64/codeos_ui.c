/* CodeOS ARM64 shell/desktop.
 *
 * A small, allocator-free interface layered on the ARM64 framebuffer and
 * input HAL.  It is the first step toward the CodeOS ARM laptop desktop: it
 * uses the same serial console, ramfb surface and virtio input path as the
 * x86_64 build, without pulling in the x86_64-only userspace/Qt stack.
 *
 * Two output paths are fed from one log: the framebuffer (when ramfb exists)
 * and COM1 via kprintf, so a headless `-nographic` boot is fully usable.
 */
#include "codeos_ui.h"
#include "display.h"
#include "fb.h"
#include "input.h"
#include "pci.h"
#include "rtc.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/types.h"

#define BAR_H    40
#define SIDE_W   220
#define LOG_ROWS 24
#define LOG_COLS 96
#define CMD_MAX  64

static const char *g_views[] = { "Terminal", "System", "Devices", "About" };
#define VIEW_COUNT ((int)(sizeof(g_views) / sizeof(g_views[0])))

static char g_log[LOG_ROWS][LOG_COLS];
static int  g_rows;
static int  g_sel;
static int  g_dirty = 1;
static char g_cmd[CMD_MAX];
static int  g_cmdlen;
static int  g_px = -1, g_py = -1;

/* ── log ──────────────────────────────────────────────────────────────── */

static void ui_log(const char *s) {
    if (!s) return;
    kprintf("%s\n", s);

    if (g_rows == LOG_ROWS) {
        for (int i = 1; i < LOG_ROWS; i++)
            for (int c = 0; c < LOG_COLS; c++)
                g_log[i - 1][c] = g_log[i][c];
        g_rows--;
    }
    strncpy_safe(g_log[g_rows++], s, LOG_COLS);
    g_dirty = 1;
}

/* ── commands ─────────────────────────────────────────────────────────── */

static void cmd_help(void) {
    ui_log("Commands:");
    ui_log("  help     show this list");
    ui_log("  ver      CodeOS version and target");
    ui_log("  date     ARM generic timer uptime");
    ui_log("  pci      list discovered PCIe devices on COM1");
    ui_log("  about    about this ARM64 port");
    ui_log("  clear    clear the shell log");
    ui_log("  echo <text>  print text");
}

static void cmd_ver(void) {
    ui_log("CodeOS 1.0 - ARM64 (QEMU virt early port)");
}

static void cmd_date(void) {
    char b[LOG_COLS];
    snprintf(b, sizeof(b), "uptime: %u s", (unsigned)rtc_read_wallclock());
    ui_log(b);
}

static void cmd_pci(void) {
    char b[LOG_COLS];
    int n = pci_device_count();
    snprintf(b, sizeof(b), "pci: %d device(s) found (details on COM1)", n);
    ui_log(b);
    pci_print_devices();
}

static void cmd_about(void) {
    ui_log("CodeOS ARM64 is the early laptop-oriented port.");
    ui_log("Today: PL011, generic timer, fw_cfg, ramfb, PCIe ECAM,");
    ui_log("polled virtio-tablet/keyboard and this shell.");
    ui_log("Still needed for real laptops: GIC, FDT discovery,");
    ui_log("userspace ABI, filesystem and a full desktop stack.");
}

static void run_command(void) {
    char line[CMD_MAX];
    strncpy_safe(line, g_cmd, sizeof(line));
    g_cmd[0] = 0;
    g_cmdlen = 0;
    g_dirty = 1;

    if (!line[0]) return;
    ui_log(line);

    if (!strcmp(line, "help"))        cmd_help();
    else if (!strcmp(line, "ver"))    cmd_ver();
    else if (!strcmp(line, "date"))   cmd_date();
    else if (!strcmp(line, "pci"))    cmd_pci();
    else if (!strcmp(line, "about"))  cmd_about();
    else if (!strcmp(line, "clear")) { g_rows = 0; g_dirty = 1; }
    else if (!strncmp(line, "echo ", 5)) ui_log(line + 5);
    else ui_log("unknown command - type 'help'");
}

/* ── rendering ────────────────────────────────────────────────────────── */

static void render(void) {
    uint32_t w = fb_getwidth();
    uint32_t h = fb_getheight();
    if (!w || !h) return;      /* headless: COM1 is the only output */

    display_wallpaper();

    /* Title bar */
    fb_fillrect(0, 0, w, BAR_H, 0x111827);
    fb_fillrect(0, BAR_H, w, 2, 0x2563eb);
    fb_drawstr_px(16, 12, "CodeOS 1.0 - ARM64", 0xe8eefc, 0x111827);
    char up[32];
    snprintf(up, sizeof(up), "up %u s", (unsigned)rtc_read_wallclock());
    fb_drawstr_px((int)(w - 120), 12, up, 0x9fb3d9, 0x111827);

    /* Sidebar */
    fb_fillrect(0, BAR_H + 2, SIDE_W, h - BAR_H - 2, 0x0b1220);
    for (int i = 0; i < VIEW_COUNT; i++) {
        int y = (int)(BAR_H + 20 + (uint32_t)i * 30);
        if (i == g_sel) {
            fb_fillrect(8, (uint32_t)y - 4, SIDE_W - 16, 26, 0x1d4ed8);
        }
        fb_drawstr_px(20, y, g_views[i],
                      i == g_sel ? 0xffffff : 0x9fb3d9,
                      i == g_sel ? 0x1d4ed8 : 0x0b1220);
    }

    /* Content log */
    int x0 = (int)SIDE_W + 20;
    int y0 = (int)BAR_H + 20;
    fb_drawstr_px(x0, y0, "CodeOS shell - type 'help' and press Enter",
                  0x93c5fd, 0x000000);
    for (int r = 0; r < g_rows; r++) {
        int y = y0 + 26 + r * 18;
        if ((uint32_t)y + 16 >= h) break;
        fb_drawstr_px(x0, y, g_log[r], 0xdbe6f7, 0x000000);
    }

    if (g_sel == 0) {
        fb_fillrect((uint32_t)x0, h - 34, w - (uint32_t)x0 - 12, 26, 0x111827);
        fb_drawstr_px(x0 + 4, (int)h - 28, g_cmd, 0xe8eefc, 0x111827);
    }

    if (g_px >= 0 && g_py >= 0) display_draw_pointer(g_px, g_py);
}

/* ── input ────────────────────────────────────────────────────────────── */

static void on_key(int key) {
    switch (key) {
    case IN_KEY_UP:
        g_sel = (g_sel + VIEW_COUNT - 1) % VIEW_COUNT;
        g_dirty = 1;
        return;
    case IN_KEY_DOWN:
        g_sel = (g_sel + 1) % VIEW_COUNT;
        g_dirty = 1;
        return;
    case IN_KEY_BACK:
        g_sel = 0;
        g_dirty = 1;
        return;
    case IN_KEY_ENTER:
        if (g_sel == 0) {
            run_command();
        } else {
            char b[LOG_COLS];
            snprintf(b, sizeof(b), "%s - %s", g_views[g_sel],
                     g_sel == 1 ? "ARM64 HAL: serial, timer, ramfb, PCIe, virtio input"
                     : g_sel == 2 ? "use 'pci' in Terminal for the device list"
                                  : "see docs/HARDWARE.md in the source tree");
            ui_log(b);
        }
        return;
    case IN_KEY_LEFT:
    case IN_KEY_RIGHT:
        return;
    default:
        break;
    }

    if (g_sel != 0) return;          /* only the Terminal accepts text */

    if (key == '\b' || key == 0x7f) {
        if (g_cmdlen > 0) g_cmd[--g_cmdlen] = 0;
        g_dirty = 1;
    } else if (key >= 32 && key < 127 && g_cmdlen < CMD_MAX - 1) {
        g_cmd[g_cmdlen++] = (char)key;
        g_cmd[g_cmdlen] = 0;
        g_dirty = 1;
    }
}

static void on_event(const input_event_t *e) {
    if (e->type == IN_KEY) {
        on_key(e->key);
        return;
    }
    if (e->type == IN_TOUCH_DOWN || e->type == IN_TOUCH_MOVE ||
        e->type == IN_TOUCH_UP) {
        g_px = e->x;
        g_py = e->y;
        g_dirty = 1;
        if (e->x >= 0 && (uint32_t)e->x < SIDE_W && e->y >= (int)BAR_H) {
            int idx = (e->y - (int)BAR_H - 16) / 30;
            if (idx >= 0 && idx < VIEW_COUNT) g_sel = idx;
        }
    }
}

/* ── entry ────────────────────────────────────────────────────────────── */

void codeos_ui_run(void) {
    ui_log("CodeOS ARM64 shell ready.");
    ui_log("Type 'help' for commands.");
    render();

    for (;;) {
        input_event_t e;
        int did = 0;
        while (input_poll(&e)) {
            on_event(&e);
            did = 1;
        }
        if (did || g_dirty) {
            render();
            g_dirty = 0;
        }
        for (volatile int spin = 0; spin < 20000; spin++) { }
    }
}

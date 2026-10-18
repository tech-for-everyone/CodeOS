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
#include <time.h>

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
#define CELL_GAP  8
#define CELL_H    57
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
#define COL_OK      0xFFA6E3A1
#define COL_WARN    0xFFF9E2AF
#define COL_ERR     0xFFF38BA8
#define COL_PANEL   0xFF20203A

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

/* ── Per-app screens ──────────────────────────────────────────────────
 * The framework apps are headless: they own their model state and print
 * to the console, while the host is their compositor (see the file
 * header).  Each registry name therefore gets a real screen below, and
 * the interactive ones keep their state in `ast` — reset on launch.
 * The host tests call the same entry points the mouse and keyboard
 * handlers use (calc_input, note_input, dial_input, ...). */

static struct {
    /* Calculator */
    char calc_entry[20]; /* digits typed for the current operand    */
    long calc_acc;       /* completed left-hand value               */
    long calc_cur;       /* current operand value                   */
    int  calc_op;        /* 0 none, 1 +, 2 -, 3 *, 4 /              */
    int  calc_err;       /* divide by zero                          */
    /* Phone */
    char dial[16];
    int  calling;
    /* Notes */
    char notes[384];
    int  notes_len;
    /* Terminal */
    char term_lines[7][44];
    int  term_cnt;
    char term_cur[44];
    int  term_len;
    /* Music */
    int  music_pos;      /* 0..100 */
    int  music_playing;
    /* Settings toggles */
    unsigned settings;
    /* Camera */
    int  shots;
    /* Gallery / SMS selections */
    int  tile_sel;
    int  sms_sel;
} ast;

#define SET_WIFI 0x1u
#define SET_BT   0x2u
#define SET_DARK 0x4u
#define SET_DND  0x8u
#define SET_AIR  0x10u

static void app_state_reset(void) {
    memset(&ast, 0, sizeof(ast));
    ast.settings = SET_DARK; /* sensible defaults for a first boot */
}

static void text_c(int y, uint32_t col, const char *s) {
    int x = (SCREEN_W - (int)strlen(s) * 8) / 2;
    if (x < 0) x = 0;
    wm_text(x, y, col, s);
}

static void text_r(int xr, int y, uint32_t col, const char *s) {
    wm_text(xr - (int)strlen(s) * 8, y, col, s);
}

static const char *active_name(void) {
    zircapp_app_t *a = zircapp_get(active_app);
    return a ? a->name : "";
}

/* ── Clock: seven-segment display fed by the kernel RTC ── */

/* Segment masks, bit0=a (top) .. bit6=g (middle). */
static int seg_mask(int d) {
    static const int m[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    return (d >= 0 && d <= 9) ? m[d] : 0;
}

static void seg_digit(int x, int y, int w, int h, int t, int digit, uint32_t col) {
    int m = seg_mask(digit);
    int hh = h / 2;
    if (m & 0x01) wm_fill(x + t, y, w - 2 * t, t, col);              /* a */
    if (m & 0x02) wm_fill(x + w - t, y + t, t, hh - t, col);         /* b */
    if (m & 0x04) wm_fill(x + w - t, y + hh, t, hh - t, col);        /* c */
    if (m & 0x08) wm_fill(x + t, y + h - t, w - 2 * t, t, col);      /* d */
    if (m & 0x10) wm_fill(x, y + hh, t, hh - t, col);                /* e */
    if (m & 0x20) wm_fill(x, y + t, t, hh - t, col);                 /* f */
    if (m & 0x40) wm_fill(x + t, y + hh - t / 2, w - 2 * t, t, col); /* g */
}

/* "HH:MM:SS" for a seconds count (day-wrapped). */
static void clock_hms(long secs, char out[9]) {
    secs %= 86400;
    if (secs < 0) secs += 86400;
    int h = (int)(secs / 3600);
    int m = (int)((secs / 60) % 60);
    int s = (int)(secs % 60);
    out[0] = (char)('0' + h / 10); out[1] = (char)('0' + h % 10);
    out[2] = ':';
    out[3] = (char)('0' + m / 10); out[4] = (char)('0' + m % 10);
    out[5] = ':';
    out[6] = (char)('0' + s / 10); out[7] = (char)('0' + s % 10);
    out[8] = 0;
}

/* Wall clock via the kernel CMOS RTC (GET_INFO cmd 3).  Falls back to
 * the system clock (seconds since boot) when there is no RTC. */
static int clock_now(char hms[9], char date[12], int *is_rtc) {
    char buf[24];
    if (sys_get_info(3, buf, (int)sizeof(buf)) > 0 && buf[10] == ' ') {
        memcpy(hms, buf + 11, 8);
        hms[8] = 0;
        memcpy(date, buf, 10);
        date[10] = 0;
        *is_rtc = 1;
        return 1;
    }
    clock_hms((long)time(NULL), hms);
    date[0] = 0;
    *is_rtc = 0;
    return 0;
}

static void draw_clock(void) {
    char hms[9], date[12];
    int is_rtc = 0;
    clock_now(hms, date, &is_rtc);

    int w = 30, h = 54, t = 6, gap = 8;
    int total = 6 * w + 2 * 14 + 7 * gap;
    int x = (SCREEN_W - total) / 2;
    int y = HEADER_H + 84;
    for (int i = 0; hms[i]; i++) {
        if (hms[i] == ':') {
            wm_fill(x + 4, y + h / 3, 6, 6, COL_ACCENT);
            wm_fill(x + 4, y + 2 * h / 3 - 6, 6, 6, COL_ACCENT);
            x += 14 + gap;
        } else {
            seg_digit(x, y, w, h, t, hms[i] - '0', COL_ACCENT);
            x += w + gap;
        }
    }

    if (is_rtc && date[0])
        text_c(y + h + 22, COL_TEXT, date);
    text_c(y + h + 46, COL_FAINT,
           is_rtc ? "hardware clock (CMOS RTC)" : "system clock (uptime, no RTC)");
}

/* ── Calculator: integer arithmetic, left-to-right (no precedence) ── */

#define CALC_X0  12
#define CALC_Y0  128
#define CALC_CW  89
#define CALC_CH  81
#define CALC_GAP 6

static void calc_reset(void) {
    ast.calc_acc = 0;
    ast.calc_cur = 0;
    ast.calc_op = 0;
    ast.calc_err = 0;
    ast.calc_entry[0] = 0;
}

static long calc_parse(const char *s) {
    long v = 0;
    while (*s) v = v * 10 + (*s++ - '0');
    return v;
}

static void calc_apply(void) {
    switch (ast.calc_op) {
    case 1: ast.calc_acc += ast.calc_cur; break;
    case 2: ast.calc_acc -= ast.calc_cur; break;
    case 3: ast.calc_acc *= ast.calc_cur; break;
    case 4:
        if (ast.calc_cur == 0) {
            ast.calc_err = 1;
            ast.calc_acc = 0;
        } else {
            ast.calc_acc /= ast.calc_cur;
        }
        break;
    default: ast.calc_acc = ast.calc_cur; break;
    }
}

static void calc_input(int ch) {
    if (ch >= '0' && ch <= '9') {
        if (ast.calc_err) calc_reset();
        int n = (int)strlen(ast.calc_entry);
        if (n < (int)sizeof(ast.calc_entry) - 1) {
            ast.calc_entry[n] = (char)ch;
            ast.calc_entry[n + 1] = 0;
            ast.calc_cur = calc_parse(ast.calc_entry);
        }
        return;
    }
    if (ch == 'c' || ch == 'C') { calc_reset(); return; }
    if (ch == 'B' || ch == 8) { /* B = on-screen key, 8 = Backspace */
        if (ast.calc_err) { calc_reset(); return; }
        int n = (int)strlen(ast.calc_entry);
        if (n > 0) {
            ast.calc_entry[n - 1] = 0;
            ast.calc_cur = calc_parse(ast.calc_entry);
        }
        return;
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
        int op = ch == '+' ? 1 : ch == '-' ? 2 : ch == '*' ? 3 : 4;
        if (ast.calc_err) calc_reset();
        if (ast.calc_op) calc_apply();
        else ast.calc_acc = ast.calc_cur;
        if (!ast.calc_err) ast.calc_op = op;
        ast.calc_entry[0] = 0;
        ast.calc_cur = 0;
        return;
    }
    if (ch == '=' || ch == '\r' || ch == '\n') {
        if (ast.calc_err) { calc_reset(); return; }
        if (ast.calc_op) {
            calc_apply();
            ast.calc_op = 0;
        } else {
            ast.calc_acc = ast.calc_cur;
        }
        ast.calc_entry[0] = 0;
        ast.calc_cur = ast.calc_acc;
        return;
    }
}

/* Key under (x, y): the grid is 4x5, with 0 spanning three columns and
 * = one tall key in the last column, spanning the last two rows. */
static char calc_key_at(int x, int y) {
    static const char g[3][4] = {
        { 'C', 'B', '/', '*' },
        { '7', '8', '9', '-' },
        { '4', '5', '6', '+' },
    };
    int col = (x - CALC_X0) / (CALC_CW + CALC_GAP);
    int row = (y - CALC_Y0) / (CALC_CH + CALC_GAP);
    if (x < CALC_X0 || y < CALC_Y0 || col > 3 || row > 4)
        return 0;
    if (row < 3) return g[row][col];
    if (col == 3) return '=';
    if (row == 3) {
        static const char r3[3] = { '1', '2', '3' };
        return r3[col];
    }
    return '0';
}

static void calc_key_draw(int x, int y, int w, int h, char k, uint32_t bg) {
    char lbl[4];
    wm_fill(x, y, w, h, bg);
    if (k == 'B') {
        lbl[0] = 'D'; lbl[1] = 'E'; lbl[2] = 'L'; lbl[3] = 0;
    } else {
        lbl[0] = k; lbl[1] = 0;
    }
    wm_text(x + (w - (int)strlen(lbl) * 8) / 2, y + (h - 16) / 2, COL_TEXT, lbl);
}

static void draw_calc(void) {
    char buf[32];
    const uint32_t key_bg = COL_CELL;
    const uint32_t op_bg  = 0xFF2A3A5E;
    const uint32_t clr_bg = 0xFF5E2A2A;

    wm_fill(CALC_X0, HEADER_H + 10, SCREEN_W - 2 * CALC_X0, 62, 0xFF11111B);
    if (ast.calc_err) {
        text_r(SCREEN_W - CALC_X0 - 10, HEADER_H + 34, COL_ERR, "ERR");
    } else {
        const char *shown = ast.calc_entry[0] ? ast.calc_entry : 0;
        if (shown) {
            text_r(SCREEN_W - CALC_X0 - 10, HEADER_H + 34, COL_TEXT, shown);
        } else {
            snprintf(buf, sizeof(buf), "%ld", ast.calc_acc);
            text_r(SCREEN_W - CALC_X0 - 10, HEADER_H + 34, COL_TEXT, buf);
        }
    }
    if (ast.calc_op) {
        const char *ops = "?";
        switch (ast.calc_op) {
        case 1: ops = "+"; break;
        case 2: ops = "-"; break;
        case 3: ops = "*"; break;
        case 4: ops = "/"; break;
        }
        snprintf(buf, sizeof(buf), "%ld %s", ast.calc_acc, ops);
        wm_text(CALC_X0 + 10, HEADER_H + 20, COL_FAINT, buf);
    }

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            char k = calc_key_at(CALC_X0 + col * (CALC_CW + CALC_GAP),
                                 CALC_Y0 + row * (CALC_CH + CALC_GAP));
            uint32_t bg = (k == 'C' || k == 'B') ? clr_bg
                        : (k >= '0' && k <= '9') ? key_bg : op_bg;
            calc_key_draw(CALC_X0 + col * (CALC_CW + CALC_GAP),
                          CALC_Y0 + row * (CALC_CH + CALC_GAP),
                          CALC_CW, CALC_CH, k, bg);
        }
    }
    int ry = CALC_Y0 + 3 * (CALC_CH + CALC_GAP);
    for (int col = 0; col < 3; col++) {
        char k = calc_key_at(CALC_X0 + col * (CALC_CW + CALC_GAP), ry);
        calc_key_draw(CALC_X0 + col * (CALC_CW + CALC_GAP), ry,
                      CALC_CW, CALC_CH, k, key_bg);
    }
    calc_key_draw(CALC_X0 + 3 * (CALC_CW + CALC_GAP), ry,
                  CALC_CW, 2 * CALC_CH + CALC_GAP, '=', op_bg);
    int zy = ry + CALC_CH + CALC_GAP;
    for (int col = 0; col < 3; col++) {
        calc_key_draw(CALC_X0 + col * (CALC_CW + CALC_GAP), zy,
                      CALC_CW, CALC_CH, '0', key_bg);
    }
}

/* ── Phone: dial pad + call state ── */

#define DIAL_X0  60
#define DIAL_Y0  156
#define DIAL_CW  90
#define DIAL_CH  52
#define DIAL_GAP 6

static void dial_input(int ch) {
    int n = (int)strlen(ast.dial);
    if (ch == 8 || ch == 'B') {
        if (n > 0) ast.dial[n - 1] = 0;
        return;
    }
    if ((ch >= '0' && ch <= '9') || ch == '*' || ch == '#') {
        if (n < 12) {
            ast.dial[n] = (char)ch;
            ast.dial[n + 1] = 0;
        }
    }
}

static char dial_key_at(int x, int y) {
    static const char g[4][3] = {
        { '1', '2', '3' },
        { '4', '5', '6' },
        { '7', '8', '9' },
        { '*', '0', '#' },
    };
    int col = (x - DIAL_X0) / (DIAL_CW + DIAL_GAP);
    int row = (y - DIAL_Y0) / (DIAL_CH + DIAL_GAP);
    if (x < DIAL_X0 || y < DIAL_Y0 || col > 2 || row > 3)
        return 0;
    return g[row][col];
}

static int dial_call_at(int x, int y) {
    return x >= DIAL_X0 && x < DIAL_X0 + 3 * DIAL_CW + 2 * DIAL_GAP &&
           y >= DIAL_Y0 + 4 * (DIAL_CH + DIAL_GAP) + 8 &&
           y < DIAL_Y0 + 4 * (DIAL_CH + DIAL_GAP) + 8 + 46;
}

static void draw_phone(void) {
    char buf[36];
    if (ast.calling) {
        snprintf(buf, sizeof(buf), "Calling %s ...", ast.dial);
        text_c(HEADER_H + 20, COL_OK, buf);
    } else if (ast.dial[0]) {
        text_c(HEADER_H + 20, COL_TEXT, ast.dial);
    } else {
        text_c(HEADER_H + 20, COL_FAINT, "enter a number");
    }

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int x = DIAL_X0 + col * (DIAL_CW + DIAL_GAP);
            int y = DIAL_Y0 + row * (DIAL_CH + DIAL_GAP);
            wm_fill(x, y, DIAL_CW, DIAL_CH, COL_CELL);
            char k = dial_key_at(x + 2, y + 2);
            char lbl[2] = { k, 0 };
            wm_text(x + (DIAL_CW - 8) / 2, y + (DIAL_CH - 16) / 2, COL_TEXT, lbl);
        }
    }

    int by = DIAL_Y0 + 4 * (DIAL_CH + DIAL_GAP) + 8;
    uint32_t bc = ast.calling ? COL_ERR : (ast.dial[0] ? COL_OK : COL_CELL_HI);
    wm_fill(DIAL_X0, by, 3 * DIAL_CW + 2 * DIAL_GAP, 46, bc);
    text_c(by + 15, 0xFF11111B, ast.calling ? "End call" : "Call");
}

/* ── Notes: one buffer, wrapped, Backspace deletes ── */

#define NOTE_COLS 44
#define NOTE_ROWS 16

static void note_input(int ch) {
    if (ch == 8) {
        if (ast.notes_len > 0) ast.notes[--ast.notes_len] = 0;
        return;
    }
    if (ch == '\r') ch = '\n';
    if ((ch == '\n' || (ch >= 32 && ch < 127)) &&
        ast.notes_len < (int)sizeof(ast.notes) - 1) {
        ast.notes[ast.notes_len++] = (char)ch;
        ast.notes[ast.notes_len] = 0;
    }
}

static void draw_notes(void) {
    int x = 16, y = HEADER_H + 18;
    int col = 0, row = 0;
    char line[NOTE_COLS + 1];

    for (int i = 0; i <= ast.notes_len; i++) {
        char c = (i < ast.notes_len) ? ast.notes[i] : 0;
        if (c == '\n' || (i == ast.notes_len && col > 0)) {
            line[col] = 0;
            wm_text(x, y + row * 20, COL_TEXT, line);
            row++;
            col = 0;
            if (c == 0) break;
            continue;
        }
        if (i == ast.notes_len) break;
        if (col >= NOTE_COLS) {
            line[col] = 0;
            wm_text(x, y + row * 20, COL_TEXT, line);
            row++;
            col = 0;
        }
        line[col++] = c;
    }
    if (col == 0 && row == 0) {
        wm_text(x, y, COL_FAINT, "Empty note - just type.");
    }
    /* cursor */
    wm_fill(x + col * 8, y + row * 20, 8, 16, COL_ACCENT);
    wm_text(x, SCREEN_H - FOOTER_H - 30, COL_FAINT,
            "keys type - Backspace deletes - 384 bytes");
}

/* ── Terminal: echo + a few builtins, 7-line scrollback ── */

static void term_push(const char *s) {
    if (ast.term_cnt == 7) {
        for (int i = 1; i < 7; i++)
            memcpy(ast.term_lines[i - 1], ast.term_lines[i], 44);
        ast.term_cnt = 6;
    }
    snprintf(ast.term_lines[ast.term_cnt], 44, "%s", s);
    ast.term_cnt++;
}

static void term_input(int ch) {
    if (ch == 8) {
        if (ast.term_len > 0) ast.term_cur[--ast.term_len] = 0;
        return;
    }
    if (ch == '\r' || ch == '\n') {
        char line[44];
        snprintf(line, sizeof(line), "%s", ast.term_cur);
        if (!strcmp(line, "help")) {
            term_push("commands: help apps time about clear");
        } else if (!strcmp(line, "apps")) {
            char b[40];
            snprintf(b, sizeof(b), "%d apps registered", zircapp_count());
            term_push(b);
        } else if (!strcmp(line, "time")) {
            char hms[9], date[12];
            int r = 0;
            clock_now(hms, date, &r);
            char b[40];
            if (r && date[0]) snprintf(b, sizeof(b), "%s %s", date, hms);
            else snprintf(b, sizeof(b), "%s (uptime)", hms);
            term_push(b);
        } else if (!strcmp(line, "about")) {
            term_push("ZircApp host on CodeOS - type help");
        } else if (!strcmp(line, "clear")) {
            ast.term_cnt = 0;
        } else if (line[0]) {
            term_push(line);
        }
        ast.term_len = 0;
        ast.term_cur[0] = 0;
        return;
    }
    if (ch >= 32 && ch < 127 && ast.term_len < 40) {
        ast.term_cur[ast.term_len++] = (char)ch;
        ast.term_cur[ast.term_len] = 0;
    }
}

static void draw_terminal(void) {
    int y = HEADER_H + 16;
    for (int i = 0; i < ast.term_cnt; i++) {
        wm_text(12, y + i * 20, COL_DIM, ast.term_lines[i]);
    }
    int py = y + ast.term_cnt * 20;
    wm_text(12, py, COL_OK, "zircapp:~$");
    char buf[48];
    snprintf(buf, sizeof(buf), "%s", ast.term_cur);
    wm_text(12 + 11 * 8, py, COL_TEXT, buf);
    int cx = 12 + 11 * 8 + ast.term_len * 8;
    wm_fill(cx, py, 8, 16, COL_ACCENT);
}

/* ── Music: transport + progress that advances on TICK ── */

static void music_toggle(void) {
    ast.music_playing = !ast.music_playing;
}

static void music_tick(void) {
    if (ast.music_playing) {
        ast.music_pos += 2;
        if (ast.music_pos > 100) ast.music_pos = 0;
    }
}

static int music_button_at(int x, int y) {
    return x >= 176 && x < 224 && y >= 420 && y < 468;
}

static void draw_music(void) {
    /* album block, pretending to be cover art */
    wm_fill(100, HEADER_H + 26, 200, 200, COL_PANEL);
    wm_fill(120, HEADER_H + 46, 160, 160, 0xFF2E4A7A);
    wm_fill(150, HEADER_H + 76, 100, 100, COL_ACCENT);
    text_c(HEADER_H + 256, COL_TEXT, "Zircon Theme");
    text_c(HEADER_H + 278, COL_FAINT, "The ZircApp Ensemble");

    int py = HEADER_H + 318;
    wm_fill(40, py, SCREEN_W - 80, 10, COL_PANEL);
    wm_fill(40, py, (SCREEN_W - 80) * ast.music_pos / 100, 10, COL_ACCENT);
    wm_text(40, py + 18, COL_FAINT, "0:00");
    {
        char t[16];
        snprintf(t, sizeof(t), "1:%02d", ast.music_pos);
        text_r(SCREEN_W - 40, py + 18, COL_FAINT, t);
    }

    uint32_t bc = ast.music_playing ? COL_ACCENT : COL_CELL;
    wm_fill(176, 420, 48, 48, bc);
    if (ast.music_playing) {
        wm_fill(188, 434, 8, 20, 0xFF11111B);
        wm_fill(204, 434, 8, 20, 0xFF11111B);
    } else {
        wm_fill(192, 434, 16, 20, 0xFF11111B); /* play triangle stand-in */
    }
    text_c(482, COL_FAINT, ast.music_playing ? "playing" : "paused - click to play");
}

/* ── Settings: five toggle rows ── */

static const char *kSetNames[5] = {
    "Wi-Fi", "Bluetooth", "Dark Mode", "Do Not Disturb", "Airplane Mode"
};
static const unsigned kSetBits[5] = {
    SET_WIFI, SET_BT, SET_DARK, SET_DND, SET_AIR
};

static int settings_row_at(int y) {
    int ry = HEADER_H + 14;
    for (int i = 0; i < 5; i++) {
        if (y >= ry && y < ry + 52) return i;
        ry += 60;
    }
    return -1;
}

static void settings_toggle(int row) {
    ast.settings ^= kSetBits[row];
}

static void draw_settings(void) {
    int ry = HEADER_H + 14;
    for (int i = 0; i < 5; i++) {
        int on = (ast.settings & kSetBits[i]) != 0;
        wm_fill(12, ry, SCREEN_W - 24, 52, COL_CELL);
        wm_text(28, ry + 18, COL_TEXT, kSetNames[i]);
        /* pill toggle */
        int px = SCREEN_W - 12 - 28 - 44;
        wm_fill(px, ry + 14, 44, 24, on ? COL_ACCENT : COL_PANEL);
        wm_fill(on ? px + 22 : px + 2, ry + 16, 20, 20, 0xFF11111B);
        ry += 60;
    }
}

/* ── Gallery: 2x3 tiles, click selects ── */

static int gallery_tile_at(int x, int y) {
    int gx = 12, gy = HEADER_H + 16, tw = 120, th = 150;
    for (int i = 0; i < 6; i++) {
        int tx = gx + (i % 3) * (tw + 8);
        int ty = gy + (i / 3) * (th + 8);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th)
            return i;
    }
    return -1;
}

static void draw_gallery(void) {
    static const uint32_t cols[6] = {
        0xFFE78284, 0xFFA6E3A1, 0xFF89B4FA,
        0xFFF9E2AF, 0xFFCBA6F7, 0xFF94E2D5
    };
    static const char *names[6] = {
        "Sunset", "Forest", "Ocean", "Sand", "Violet", "Mint"
    };
    for (int i = 0; i < 6; i++) {
        int tx = 12 + (i % 3) * 128;
        int ty = HEADER_H + 16 + (i / 3) * 158;
        wm_fill(tx, ty, 120, 120, cols[i]);
        if (i == ast.tile_sel) {
            wm_fill(tx - 3, ty - 3, 126, 3, COL_TEXT);
            wm_fill(tx - 3, ty + 120, 126, 3, COL_TEXT);
            wm_fill(tx - 3, ty, 3, 120, COL_TEXT);
            wm_fill(tx + 120, ty, 3, 120, COL_TEXT);
        }
        wm_text(tx + (120 - (int)strlen(names[i]) * 8) / 2, ty + 124,
                i == ast.tile_sel ? COL_TEXT : COL_DIM, names[i]);
    }
}

/* ── SMS: thread list, click selects ── */

static const char *kSmsFrom[4] = { "Mom", "ZircApp", "Ada", "Network" };
static const char *kSmsText[4] = {
    "call me back when you can",
    "welcome to Zircon!",
    "the kernel build is green",
    "your data is safe"
};

static int sms_row_at(int y) {
    int ry = HEADER_H + 14;
    for (int i = 0; i < 4; i++) {
        if (y >= ry && y < ry + 56) return i;
        ry += 64;
    }
    return -1;
}

static void draw_sms(void) {
    int ry = HEADER_H + 14;
    for (int i = 0; i < 4; i++) {
        int sel = (i == ast.sms_sel);
        wm_fill(12, ry, SCREEN_W - 24, 56, sel ? COL_CELL_HI : COL_CELL);
        wm_fill(12, ry, 4, 56, sel ? COL_ACCENT : COL_PANEL);
        wm_text(28, ry + 10, COL_TEXT, kSmsFrom[i]);
        wm_text(28, ry + 30, COL_FAINT, kSmsText[i]);
        ry += 64;
    }
    text_c(SCREEN_H - FOOTER_H - 26, COL_FAINT,
           ast.sms_sel >= 0 ? "thread selected" : "click a thread");
}

/* ── Weather: one card + a four-day strip ── */

static void draw_weather(void) {
    wm_fill(12, HEADER_H + 16, SCREEN_W - 24, 150, COL_PANEL);
    text_c(HEADER_H + 34, COL_TEXT, "Zircon City");
    text_c(HEADER_H + 62, COL_ACCENT, "21 C");
    text_c(HEADER_H + 96, COL_DIM, "partly cloudy");
    text_c(HEADER_H + 118, COL_FAINT, "wind 12 km/h - humidity 48%");

    static const char *days[4] = { "Mon", "Tue", "Wed", "Thu" };
    static const char *tems[4] = { "22 C", "19 C", "24 C", "18 C" };
    for (int i = 0; i < 4; i++) {
        int cx = 12 + i * 95;
        wm_fill(cx, HEADER_H + 186, 88, 96, COL_CELL);
        wm_text(cx + (88 - 3 * 8) / 2, HEADER_H + 200, COL_DIM, days[i]);
        wm_text(cx + (88 - 4 * 8) / 2, HEADER_H + 224, COL_TEXT, tems[i]);
        wm_fill(cx + 20, HEADER_H + 250, 48, 6, i == 0 ? COL_ACCENT : COL_PANEL);
    }
}

/* ── Browser: URL bar + an OpenWeb card ── */

static void draw_browser_app(void) {
    wm_fill(12, HEADER_H + 14, SCREEN_W - 104, 36, COL_PANEL);
    wm_text(24, HEADER_H + 24, COL_TEXT, "about:zircon");
    wm_fill(SCREEN_W - 82, HEADER_H + 14, 70, 36, COL_ACCENT);
    text_c(HEADER_H + 24, 0xFF11111B, "Go");

    wm_fill(12, HEADER_H + 64, SCREEN_W - 24, 190, COL_CELL);
    wm_text(28, HEADER_H + 82, COL_ACCENT, "OpenWeb engine");
    wm_text(28, HEADER_H + 108, COL_DIM, "Rust HTTP + HTML renderer,");
    wm_text(28, HEADER_H + 128, COL_DIM, "no WebKit, no TLS stack.");
    wm_text(28, HEADER_H + 158, COL_FAINT, "The windowed browser is the");
    wm_text(28, HEADER_H + 178, COL_FAINT, "openweb app on the desktop.");
    wm_text(28, HEADER_H + 210, COL_TEXT, "try: http://10.0.2.2:8095/");
}

/* ── Camera: viewfinder, clicking snaps a shot ── */

static void draw_camera(void) {
    wm_fill(8, HEADER_H + 14, SCREEN_W - 16, 380, 0xFF0C0C14);
    /* corner brackets */
    uint32_t c = COL_ACCENT;
    wm_fill(20, HEADER_H + 26, 36, 4, c);   wm_fill(20, HEADER_H + 26, 4, 36, c);
    wm_fill(SCREEN_W - 56, HEADER_H + 26, 36, 4, c);
    wm_fill(SCREEN_W - 24, HEADER_H + 26, 4, 36, c);
    wm_fill(20, HEADER_H + 368, 36, 4, c);  wm_fill(20, HEADER_H + 336, 4, 36, c);
    wm_fill(SCREEN_W - 56, HEADER_H + 368, 36, 4, c);
    wm_fill(SCREEN_W - 24, HEADER_H + 336, 4, 36, c);
    if (ast.shots > 0) {
        char b[32];
        snprintf(b, sizeof(b), "captured %d photo%s", ast.shots,
                 ast.shots == 1 ? "" : "s");
        text_c(HEADER_H + 210, COL_OK, b);
    } else {
        text_c(HEADER_H + 210, COL_FAINT, "click to snap a photo");
    }
    /* shutter */
    wm_fill(184, HEADER_H + 414, 32, 32, ast.shots ? COL_ERR : COL_CELL_HI);
}

/* ── About: identity + kernel info ── */

static void draw_about(void) {
    char kver[64];
    if (sys_get_info(0, kver, (int)sizeof(kver)) <= 0)
        snprintf(kver, sizeof(kver), "CodeOS");
    wm_fill(172, HEADER_H + 24, 56, 56, COL_ACCENT);
    text_c(HEADER_H + 92, COL_TEXT, "Zircon");
    text_c(HEADER_H + 114, COL_FAINT, "the Better Android");
    text_c(HEADER_H + 152, COL_DIM, "native C apps - no VM - no Google");
    text_c(HEADER_H + 182, COL_DIM, "ZircApp framework on the CodeOS kernel");
    text_c(HEADER_H + 216, COL_FAINT, kver);
    {
        char b[48];
        snprintf(b, sizeof(b), "%d apps registered", zircapp_count());
        text_c(HEADER_H + 246, COL_FAINT, b);
    }
    text_c(HEADER_H + 286, COL_FAINT, "v1.0.0");
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

    /* header: back button + app name */
    wm_fill(0, 0, SCREEN_W, HEADER_H, COL_BAR);
    wm_text(12, (HEADER_H - 16) / 2, COL_ACCENT, "< Home");
    wm_text(96, (HEADER_H - 16) / 2, COL_TEXT, a->name);

    /* body, then the app's own screen */
    wm_fill(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H - FOOTER_H, COL_BG);
    const char *n = a->name;
    if (!strcmp(n, "Clock"))            draw_clock();
    else if (!strcmp(n, "Calculator"))  draw_calc();
    else if (!strcmp(n, "Phone"))       draw_phone();
    else if (!strcmp(n, "Notes"))       draw_notes();
    else if (!strcmp(n, "Terminal"))    draw_terminal();
    else if (!strcmp(n, "Music"))       draw_music();
    else if (!strcmp(n, "Settings"))    draw_settings();
    else if (!strcmp(n, "Gallery"))     draw_gallery();
    else if (!strcmp(n, "SMS"))         draw_sms();
    else if (!strcmp(n, "Weather"))     draw_weather();
    else if (!strcmp(n, "Browser"))     draw_browser_app();
    else if (!strcmp(n, "Camera"))      draw_camera();
    else if (!strcmp(n, "About"))       draw_about();
    else {
        /* No bespoke screen (Home, Files, Notifications, ...): a card. */
        wm_text(16, HEADER_H + 22, COL_TEXT, "ZircApp app running under CodeOS");
        wm_text(16, HEADER_H + 46, COL_DIM, "touch events broadcast to all apps");
        char buf[64];
        snprintf(buf, sizeof(buf), "registry index %d", active_app);
        wm_text(16, HEADER_H + 76, COL_FAINT, buf);
        snprintf(buf, sizeof(buf), "gui window id %d", a->win_id);
        wm_text(16, HEADER_H + 100, COL_FAINT, buf);
    }

    draw_status();
    wm_flush();
}

/* Route a click inside the app body to the active app's screen. */
static void app_click(int x, int y) {
    const char *n = active_name();
    if (!strcmp(n, "Calculator")) {
        char k = calc_key_at(x, y);
        if (k) { calc_input(k); draw_app_screen(); }
    } else if (!strcmp(n, "Phone")) {
        char k = dial_key_at(x, y);
        if (k) {
            dial_input(k);
            draw_app_screen();
        } else if (dial_call_at(x, y)) {
            if (ast.calling) ast.calling = 0;
            else if (ast.dial[0]) ast.calling = 1;
            draw_app_screen();
        }
    } else if (!strcmp(n, "Settings")) {
        int row = settings_row_at(y);
        if (row >= 0) { settings_toggle(row); draw_app_screen(); }
    } else if (!strcmp(n, "Music")) {
        if (music_button_at(x, y)) { music_toggle(); draw_app_screen(); }
    } else if (!strcmp(n, "Camera")) {
        ast.shots++;
        draw_app_screen();
    } else if (!strcmp(n, "Gallery")) {
        int t = gallery_tile_at(x, y);
        if (t >= 0) { ast.tile_sel = t; draw_app_screen(); }
    } else if (!strcmp(n, "SMS")) {
        int t = sms_row_at(y);
        if (t >= 0) { ast.sms_sel = t; draw_app_screen(); }
    }
    /* The static screens (Weather, Browser, About) ignore clicks. */
}

/* Route a key press to the active app's screen. */
static void app_key(int key) {
    const char *n = active_name();
    if (!strcmp(n, "Calculator")) {
        calc_input(key < 256 ? key : 0);
        draw_app_screen();
    } else if (!strcmp(n, "Notes")) {
        note_input(key < 256 ? key : 0);
        draw_app_screen();
    } else if (!strcmp(n, "Phone")) {
        dial_input(key < 256 ? key : 0);
        draw_app_screen();
    } else if (!strcmp(n, "Terminal")) {
        term_input(key < 256 ? key : 0);
        draw_app_screen();
    }
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
    app_state_reset();
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
        } else {
            app_click(x, y);
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
    } else if (mode == MODE_APP) {
        app_key(key); /* the active app's screen owns printable keys */
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
            /* Screens whose content moves on their own repaint here. */
            if (mode == MODE_APP) {
                const char *n = active_name();
                if (!strcmp(n, "Clock") || !strcmp(n, "Music")) {
                    if (!strcmp(n, "Music")) music_tick();
                    draw_app_screen();
                }
            }
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

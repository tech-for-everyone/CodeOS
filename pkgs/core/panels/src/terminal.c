#include "terminal.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
#include "mouse.h"
#include "input.h"
#include "keyboard.h"

#define TERM_W 800
#define TERM_H 600

static int term_open;
static char term_buf[4096];
static int term_pos;
static char term_hist[16][256];
static int term_hist_cnt;
static int term_hist_idx;

void terminal_open(void) {
    term_open = 1;
    term_pos = 0;
    memset(term_buf, 0, sizeof(term_buf));
    term_hist_cnt = 0;
    term_hist_idx = 0;
    fb_clear();
}

void terminal_close(void) {
    term_open = 0;
    fb_clear();
}

int terminal_is_open(void) { return term_open; }
int terminal_is_focused(void) { return 0; }
int terminal_win_idx(void) { return 0; }

void terminal_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!term_open) return;

    int x = (scr_w - TERM_W) / 2;
    int y = (scr_h - TERM_H) / 2;

    /* Glass background */
    fb_fillrect_gradient_v(x, y, TERM_W, TERM_H,
                           0x30101010, 0x20000000);

    /* Title */
    fb_drawstr_px(x + 8, y + 6, "Terminal", C_BLUE, 0);

    /* Command output area */
    int oy = y + 40;
    int oh = TERM_H - 80;
    fb_fillrect(x + 8, oy, TERM_W - 16, oh, 0x10000000);

    /* Input line */
    int iy = y + TERM_H - 36;
    fb_fillrect(x + 8, iy, TERM_W - 16, 28, 0x20000000);

    /* Prompt */
    fb_drawstr_px(x + 10, iy + 2, "$ ", C_DIM, 0);

    /* Input buffer */
    fb_drawstr_px(x + 22, iy + 2, term_buf, C_TEXT, 0);

    /* History scroll indicator */
    if (term_hist_cnt > 0) {
        fb_drawstr_px(x + TERM_W - 100, y + 8, "...", C_DIM, 0);
    }
}

int terminal_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
    int iy = scr_h - 36;

    if (my >= iy && my < iy + 28) {
        /* Click on input line - focus it */
        return 1;
    }
    return 0;
}

int terminal_keypress(int key) {
    if (!term_open) return 0;

    if (key == KEY_ENTER) {
        /* Execute command */
        if (term_hist_cnt < 16) {
            strncpy(term_hist[term_hist_cnt], term_buf, 255);
            term_hist[term_hist_cnt][255] = 0;
            term_hist_cnt++;
        }
        term_hist_idx = term_hist_cnt;
        term_pos = 0;
        memset(term_buf, 0, sizeof(term_buf));
        term_pos = 0;
        return 1;
    } else if (key == KEY_BACKSPACE) {
        if (term_pos > 0) {
            term_pos--;
            term_buf[term_pos] = 0;
        }
        return 1;
    } else if (key >= 32 && key <= 126) {
        if (term_pos < (int)sizeof(term_buf) - 1) {
            term_buf[term_pos++] = (char)key;
            term_buf[term_pos] = 0;
        }
        return 1;
    }
    return 0;
}
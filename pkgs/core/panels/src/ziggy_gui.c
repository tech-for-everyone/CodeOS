#include "ziggy_gui.h"
#include "desktop.h"
#include "windows.h"
#include "ai.h"
#include "string.h"
#include "fb.h"
#include "keyboard.h"
#include "timer.h"

#define ZG_WIN_W      480
#define ZG_WIN_H      540
#define ZG_MAX_MSGS   48
#define ZG_MSG_LEN    512
#define ZG_INPUT_MAX  200
#define ZG_PAD        10
#define ZG_INPUT_H    40
#define ZG_LINE_H     16
#define ZG_MSG_GAP    4
#define ZG_ACCENT_W   3
#define ZG_SEND_SZ    32

#define ZG_BG          C_CRUST
#define ZG_INPUT_BG    C_BASE
#define ZG_SEND_COLOR  C_BLUE
#define ZG_SEND_HOVER  C_SKY
#define ZG_TEXT        C_TEXT
#define ZG_LABEL_AI    C_MAUVE
#define ZG_LABEL_USER  C_BLUE
#define ZG_BORDER      C_SURFACE2
#define ZG_PLACEHOLDER C_SUBTEXT0

typedef enum { ZG_MSG_USER, ZG_MSG_AI } zg_type_t;

typedef struct {
    zg_type_t type;
    char text[ZG_MSG_LEN];
    uint64_t time_ms;
} zg_msg_t;

static int zg_open;
static int zg_win;
static zg_msg_t zg_msgs[ZG_MAX_MSGS];
static int zg_msg_count;
static char zg_input[ZG_INPUT_MAX];
static int zg_input_len;
static uint64_t zg_blink_start;
static int zg_cursor_vis;
static int zg_hover_send;

static int zg_cx, zg_cy, zg_cw, zg_ch;
static int zg_in_y, zg_in_h;
static int zg_chat_x, zg_chat_y, zg_chat_w, zg_chat_h;
static int zg_send_x, zg_send_y, zg_send_sz;

static void zg_compute_layout(void) {
    window_t *w = desktop_get_window(zg_win);
    if (!w) return;
    zg_cx = w->x + 4;
    zg_cy = w->y + 22;
    zg_cw = w->w - 8;
    zg_ch = w->h - 26;
    zg_in_h = ZG_INPUT_H;
    zg_in_y = zg_cy + zg_ch - zg_in_h - ZG_PAD;
    zg_chat_x = zg_cx + ZG_PAD;
    zg_chat_y = zg_cy + ZG_PAD;
    zg_chat_w = zg_cw - ZG_PAD * 2;
    zg_chat_h = zg_in_y - zg_chat_y - ZG_PAD;
    zg_send_sz = ZG_SEND_SZ;
    zg_send_x = zg_chat_x + zg_chat_w - zg_send_sz - 2;
    zg_send_y = zg_in_y + (zg_in_h - zg_send_sz) / 2;
}

static int zg_wrap_line_count(const char *text, int max_chars) {
    int count = 0;
    int pos = 0;
    int len = strlen(text);
    while (pos < len) {
        int end = pos + max_chars;
        if (end > len) end = len;
        if (end < len) {
            int bp = end;
            while (bp > pos && text[bp] != ' ') bp--;
            if (bp > pos) end = bp;
        }
        count++;
        pos = end;
        while (pos < len && text[pos] == ' ') pos++;
    }
    if (count < 1) count = 1;
    return count;
}

static int zg_msg_height(const char *text, int max_chars) {
    int lines = zg_wrap_line_count(text, max_chars);
    if (lines < 1) lines = 1;
    return ZG_LINE_H + 2 + lines * ZG_LINE_H;
}

static void zg_draw_wrapped(int x, int y, const char *text, int max_chars, uint32_t fg, uint32_t bg) {
    int pos = 0;
    int len = strlen(text);
    int line_y = y;
    while (pos < len) {
        int end = pos + max_chars;
        if (end > len) end = len;
        if (end < len) {
            int bp = end;
            while (bp > pos && text[bp] != ' ') bp--;
            if (bp > pos) end = bp;
        }
        char buf[ZG_MSG_LEN];
        int bi = 0;
        int i;
        for (i = pos; i < end && bi < ZG_MSG_LEN - 1; i++)
            buf[bi++] = text[i];
        buf[bi] = 0;
        fb_drawstr_px(x, line_y, buf, fg, bg);
        pos = end;
        while (pos < len && text[pos] == ' ') pos++;
        line_y += ZG_LINE_H;
    }
}

static void zg_add_msg(zg_type_t type, const char *text) {
    if (zg_msg_count >= ZG_MAX_MSGS) {
        for (int i = 1; i < ZG_MAX_MSGS; i++)
            zg_msgs[i - 1] = zg_msgs[i];
        zg_msg_count--;
    }
    zg_msg_t *m = &zg_msgs[zg_msg_count];
    m->type = type;
    strlcpy(m->text, text, sizeof(m->text));
    m->time_ms = timer_get_milliseconds();
    zg_msg_count++;
}

static void zg_send(void) {
    if (zg_input_len == 0) return;
    zg_input[zg_input_len] = 0;
    zg_add_msg(ZG_MSG_USER, zg_input);
    char response[ZG_MSG_LEN];
    int rlen = ai_query(zg_input, response, sizeof(response));
    char saved[ZG_INPUT_MAX];
    strlcpy(saved, zg_input, sizeof(saved));
    zg_input_len = 0;
    zg_input[0] = 0;
    zg_cursor_vis = 1;
    zg_blink_start = timer_get_milliseconds();
    if (rlen > 0)
        zg_add_msg(ZG_MSG_AI, response);
    else if (strcmp(saved, "clear") == 0)
        zg_msg_count = 0;
    else
        zg_add_msg(ZG_MSG_AI, "I stared into the void and the void shrugged.");
    desktop_redraw();
}

void ziggy_gui_open(void) {
    if (zg_open) return;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    int wx = (sw - ZG_WIN_W) / 2;
    int wy = (sh - ZG_WIN_H) / 3;
    if (wy < 30) wy = 30;
    zg_win = desktop_new_window(wx, wy, ZG_WIN_W, ZG_WIN_H, "Ziggy AI", 0xFFFFFFFF, ZG_BG);
    if (zg_win < 0) return;
    zg_open = 1;
    zg_msg_count = 0;
    zg_input_len = 0;
    zg_hover_send = 0;
    zg_cursor_vis = 1;
    zg_blink_start = timer_get_milliseconds();
    zg_add_msg(ZG_MSG_AI, "Hey, I'm Ziggy. Ask me anything about CodeOS — the kernel, GUI, drivers, network, containers, whatever. Type 'help' to see what I know.");
    desktop_set_focused(zg_win);
    desktop_redraw();
}

void ziggy_gui_close(void) {
    zg_open = 0;
}

int ziggy_gui_is_open(void) {
    return zg_open;
}

void ziggy_gui_draw(void) {
    if (!zg_open) return;
    window_t *w = desktop_get_window(zg_win);
    if (!w || !w->visible) return;
    zg_compute_layout();
    if (zg_cw < 20 || zg_ch < 20) return;

    fb_fillrect(zg_cx, zg_cy, zg_cw, zg_ch, ZG_BG);

    uint64_t now = timer_get_milliseconds();
    if (now - zg_blink_start > 500) {
        zg_cursor_vis = !zg_cursor_vis;
        zg_blink_start = now;
    }

    int max_chars = (zg_chat_w - ZG_ACCENT_W - 6) / 8;
    if (max_chars < 8) max_chars = 8;

    /* Draw messages from bottom up */
    int draw_y = zg_in_y - ZG_PAD;
    int total_msg_h[ZG_MAX_MSGS];
    for (int i = 0; i < zg_msg_count; i++)
        total_msg_h[i] = zg_msg_height(zg_msgs[i].text, max_chars) + ZG_MSG_GAP;

    for (int i = zg_msg_count - 1; i >= 0; i--) {
        int mh = total_msg_h[i];
        if (draw_y - mh < zg_chat_y) break;
        draw_y -= mh;

        zg_msg_t *m = &zg_msgs[i];
        uint32_t accent = (m->type == ZG_MSG_AI) ? ZG_LABEL_AI : ZG_LABEL_USER;
        uint32_t label_color = accent;
        uint32_t msg_bg = (m->type == ZG_MSG_AI) ? C_SURFACE0 : C_SURFACE1;

        int lx = zg_chat_x;
        int ly = draw_y;

        ui_draw_card(lx, ly, zg_chat_w, mh, msg_bg);
        fb_fillrect(lx, ly, ZG_ACCENT_W, mh, accent);

        const char *label = (m->type == ZG_MSG_AI) ? "Ziggy" : "You";
        fb_drawstr_px(lx + ZG_ACCENT_W + 4, ly + 1, label, label_color, msg_bg);

        int text_y = ly + ZG_LINE_H + 2;
        zg_draw_wrapped(lx + ZG_ACCENT_W + 4, text_y, m->text, max_chars, ZG_TEXT, msg_bg);
    }

    /* Separator line */
    fb_fillrect(zg_chat_x, zg_in_y - 1, zg_chat_w, 1, ZG_BORDER);

    /* Input field background */
    int input_w = zg_chat_w - zg_send_sz - 6;
    fb_fill_rounded_rect(zg_chat_x, zg_in_y + 2, input_w, zg_in_h - 4, 6, ZG_INPUT_BG);
    fb_draw_rounded_rect(zg_chat_x, zg_in_y + 2, input_w, zg_in_h - 4, 6, ZG_BORDER);

    /* Input text or placeholder */
    if (zg_input_len > 0) {
        char display[ZG_INPUT_MAX + 4];
        int di = 0;
        for (int i = 0; i < zg_input_len && i < ZG_INPUT_MAX; i++)
            display[di++] = zg_input[i];
        if (zg_cursor_vis && di < ZG_INPUT_MAX)
            display[di++] = '_';
        display[di] = 0;
        fb_drawstr_px(zg_chat_x + 8, zg_in_y + (zg_in_h - 16) / 2, display, ZG_TEXT, ZG_INPUT_BG);
    } else {
        fb_drawstr_px(zg_chat_x + 8, zg_in_y + (zg_in_h - 16) / 2, "Ask me anything...", ZG_PLACEHOLDER, ZG_INPUT_BG);
        if (zg_cursor_vis) {
            char cursor[2] = { '_', 0 };
            fb_drawstr_px(zg_chat_x + 8, zg_in_y + (zg_in_h - 16) / 2, cursor, ZG_LABEL_AI, ZG_INPUT_BG);
        }
    }

    /* Send button: right-pointing arrow triangle */
    uint32_t sbg = zg_hover_send ? ZG_SEND_HOVER : ZG_SEND_COLOR;
    fb_draw_shadow_layered(zg_send_x, zg_send_y, zg_send_sz, zg_send_sz, 8, 90, 4, 4);
    ui_draw_button(zg_send_x, zg_send_y, zg_send_sz, zg_send_sz, sbg, C_SURFACE2, C_TEXT);
    int cx = zg_send_x + zg_send_sz / 2 + 2;
    int cy = zg_send_y + zg_send_sz / 2;
    for (int dy = -7; dy <= 7; dy++) {
        int half = 7 - (dy < 0 ? -dy : dy);
        int lx = cx - half;
        fb_fillrect(lx, cy + dy, half * 2, 1, C_TEXT);
    }
}

int ziggy_gui_click(int mx, int my) {
    if (!zg_open) return 0;
    window_t *w = desktop_get_window(zg_win);
    if (!w || !w->visible) return 0;
    zg_compute_layout();
    if (mx < zg_cx || mx >= zg_cx + zg_cw || my < zg_cy || my >= zg_cy + zg_ch) return 0;

    /* Send button */
    if (mx >= zg_send_x && mx < zg_send_x + zg_send_sz &&
        my >= zg_send_y && my < zg_send_y + zg_send_sz) {
        zg_send();
        zg_hover_send = 0;
        return 1;
    }

    /* Focus input on click in input area */
    int input_w = zg_chat_w - zg_send_sz - 6;
    if (mx >= zg_chat_x && mx < zg_chat_x + input_w &&
        my >= zg_in_y + 2 && my < zg_in_y + zg_in_h - 2) {
        desktop_set_focused(zg_win);
        zg_cursor_vis = 1;
        zg_blink_start = timer_get_milliseconds();
        return 1;
    }

    /* Click in chat area focuses window */
    desktop_set_focused(zg_win);
    return 1;
}

void ziggy_gui_mousemove(int mx, int my) {
    if (!zg_open) return;
    zg_compute_layout();
    int hover = (mx >= zg_send_x && mx < zg_send_x + zg_send_sz &&
                 my >= zg_send_y && my < zg_send_y + zg_send_sz);
    if (hover != zg_hover_send) {
        zg_hover_send = hover;
        desktop_redraw();
    }
}

void ziggy_gui_key(int key) {
    if (!zg_open) return;

    if (key == '\x1b') return;

    if (key == '\n' || key == '\r') {
        zg_send();
        return;
    }

    if (key == '\b' || key == 0x7f) {
        if (zg_input_len > 0) {
            zg_input_len--;
            zg_input[zg_input_len] = 0;
            zg_cursor_vis = 1;
            zg_blink_start = timer_get_milliseconds();
            desktop_redraw();
        }
        return;
    }

    if (key >= ' ' && key < 0x7f && zg_input_len < ZG_INPUT_MAX - 1) {
        zg_input[zg_input_len++] = (char)key;
        zg_input[zg_input_len] = 0;
        zg_cursor_vis = 1;
        zg_blink_start = timer_get_milliseconds();
        desktop_redraw();
    }
}

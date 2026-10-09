#include "freecode_gui.h"
#include "desktop.h"
#include "windows.h"
#include "ai.h"
#include "string.h"
#include "fb.h"
#include "keyboard.h"
#include "timer.h"

#define FC_WIN_W      480
#define FC_WIN_H      540
#define FC_MAX_MSGS   48
#define FC_MSG_LEN    512
#define FC_INPUT_MAX  200
#define FC_PAD        10
#define FC_INPUT_H    40
#define FC_LINE_H     16
#define FC_MSG_GAP    4
#define FC_ACCENT_W   3
#define FC_SEND_SZ    32

#define FC_BG          C_CRUST
#define FC_INPUT_BG    C_BASE
#define FC_SEND_COLOR  C_BLUE
#define FC_SEND_HOVER  C_SKY
#define FC_TEXT        C_TEXT
#define FC_LABEL_AI    C_MAUVE
#define FC_LABEL_USER  C_BLUE
#define FC_BORDER      C_SURFACE2
#define FC_PLACEHOLDER C_SUBTEXT0

typedef enum { FC_MSG_USER, FC_MSG_AI } fc_type_t;

typedef struct {
    fc_type_t type;
    char text[FC_MSG_LEN];
    uint64_t time_ms;
} fc_msg_t;

static int fc_open;
static int fc_win;
static fc_msg_t fc_msgs[FC_MAX_MSGS];
static int fc_msg_count;
static char fc_input[FC_INPUT_MAX];
static int fc_input_len;
static uint64_t fc_blink_start;
static int fc_cursor_vis;
static int fc_hover_send;

static int fc_cx, fc_cy, fc_cw, fc_ch;
static int fc_in_y, fc_in_h;
static int fc_chat_x, fc_chat_y, fc_chat_w, fc_chat_h;
static int fc_send_x, fc_send_y, fc_send_sz;

static void fc_compute_layout(void) {
    window_t *w = desktop_get_window(fc_win);
    if (!w) return;
    fc_cx = w->x + 4;
    fc_cy = w->y + 22;
    fc_cw = w->w - 8;
    fc_ch = w->h - 26;
    fc_in_h = FC_INPUT_H;
    fc_in_y = fc_cy + fc_ch - fc_in_h - FC_PAD;
    fc_chat_x = fc_cx + FC_PAD;
    fc_chat_y = fc_cy + FC_PAD;
    fc_chat_w = fc_cw - FC_PAD * 2;
    fc_chat_h = fc_in_y - fc_chat_y - FC_PAD;
    fc_send_sz = FC_SEND_SZ;
    fc_send_x = fc_chat_x + fc_chat_w - fc_send_sz - 2;
    fc_send_y = fc_in_y + (fc_in_h - fc_send_sz) / 2;
}

static int fc_wrap_line_count(const char *text, int max_chars) {
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

static int fc_msg_height(const char *text, int max_chars) {
    int lines = fc_wrap_line_count(text, max_chars);
    if (lines < 1) lines = 1;
    return FC_LINE_H + 2 + lines * FC_LINE_H;
}

static void fc_draw_wrapped(int x, int y, const char *text, int max_chars, uint32_t fg, uint32_t bg) {
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
        char buf[FC_MSG_LEN];
        int bi = 0;
        int i;
        for (i = pos; i < end && bi < FC_MSG_LEN - 1; i++)
            buf[bi++] = text[i];
        buf[bi] = 0;
        fb_drawstr_px(x, line_y, buf, fg, bg);
        pos = end;
        while (pos < len && text[pos] == ' ') pos++;
        line_y += FC_LINE_H;
    }
}

static void fc_add_msg(fc_type_t type, const char *text) {
    if (fc_msg_count >= FC_MAX_MSGS) {
        for (int i = 1; i < FC_MAX_MSGS; i++)
            fc_msgs[i - 1] = fc_msgs[i];
        fc_msg_count--;
    }
    fc_msg_t *m = &fc_msgs[fc_msg_count];
    m->type = type;
    strlcpy(m->text, text, sizeof(m->text));
    m->time_ms = timer_get_milliseconds();
    fc_msg_count++;
}

static void fc_send(void) {
    if (fc_input_len == 0) return;
    fc_input[fc_input_len] = 0;
    fc_add_msg(FC_MSG_USER, fc_input);
    char response[FC_MSG_LEN];
    int rlen = ai_query(fc_input, response, sizeof(response));
    char saved[FC_INPUT_MAX];
    strlcpy(saved, fc_input, sizeof(saved));
    fc_input_len = 0;
    fc_input[0] = 0;
    fc_cursor_vis = 1;
    fc_blink_start = timer_get_milliseconds();
    if (rlen > 0)
        fc_add_msg(FC_MSG_AI, response);
    else if (strcmp(saved, "clear") == 0)
        fc_msg_count = 0;
    else
        fc_add_msg(FC_MSG_AI, "I stared into the void and the void shrugged.");
    desktop_redraw();
}

void freecode_gui_open(void) {
    if (fc_open) return;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    int wx = (sw - FC_WIN_W) / 2;
    int wy = (sh - FC_WIN_H) / 3;
    if (wy < 30) wy = 30;
    fc_win = desktop_new_window(wx, wy, FC_WIN_W, FC_WIN_H, "FreeCode AI", 0xFFFFFFFF, FC_BG);
    if (fc_win < 0) return;
    fc_open = 1;
    fc_msg_count = 0;
    fc_input_len = 0;
    fc_hover_send = 0;
    fc_cursor_vis = 1;
    fc_blink_start = timer_get_milliseconds();
    fc_add_msg(FC_MSG_AI, "Hey, I'm FreeCode. Ask me anything about CodeOS — the kernel, GUI, drivers, network, containers, whatever. Type 'help' to see what I know.");
    desktop_set_focused(fc_win);
    desktop_redraw();
}

void freecode_gui_close(void) {
    fc_open = 0;
}

int freecode_gui_is_open(void) {
    return fc_open;
}

void freecode_gui_draw(void) {
    if (!fc_open) return;
    window_t *w = desktop_get_window(fc_win);
    if (!w || !w->visible) return;
    fc_compute_layout();
    if (fc_cw < 20 || fc_ch < 20) return;

    fb_fillrect(fc_cx, fc_cy, fc_cw, fc_ch, FC_BG);

    uint64_t now = timer_get_milliseconds();
    if (now - fc_blink_start > 500) {
        fc_cursor_vis = !fc_cursor_vis;
        fc_blink_start = now;
    }

    int max_chars = (fc_chat_w - FC_ACCENT_W - 6) / 8;
    if (max_chars < 8) max_chars = 8;

    /* Draw messages from bottom up */
    int draw_y = fc_in_y - FC_PAD;
    int total_msg_h[FC_MAX_MSGS];
    for (int i = 0; i < fc_msg_count; i++)
        total_msg_h[i] = fc_msg_height(fc_msgs[i].text, max_chars) + FC_MSG_GAP;

    for (int i = fc_msg_count - 1; i >= 0; i--) {
        int mh = total_msg_h[i];
        if (draw_y - mh < fc_chat_y) break;
        draw_y -= mh;

        fc_msg_t *m = &fc_msgs[i];
        uint32_t accent = (m->type == FC_MSG_AI) ? FC_LABEL_AI : FC_LABEL_USER;
        uint32_t label_color = accent;
        uint32_t msg_bg = (m->type == FC_MSG_AI) ? C_SURFACE0 : C_SURFACE1;

        int lx = fc_chat_x;
        int ly = draw_y;

        ui_draw_card(lx, ly, fc_chat_w, mh, msg_bg);
        fb_fillrect(lx, ly, FC_ACCENT_W, mh, accent);

        const char *label = (m->type == FC_MSG_AI) ? "FreeCode" : "You";
        fb_drawstr_px(lx + FC_ACCENT_W + 4, ly + 1, label, label_color, msg_bg);

        int text_y = ly + FC_LINE_H + 2;
        fc_draw_wrapped(lx + FC_ACCENT_W + 4, text_y, m->text, max_chars, FC_TEXT, msg_bg);
    }

    /* Separator line */
    fb_fillrect(fc_chat_x, fc_in_y - 1, fc_chat_w, 1, FC_BORDER);

    /* Input field background */
    int input_w = fc_chat_w - fc_send_sz - 6;
    fb_fill_rounded_rect(fc_chat_x, fc_in_y + 2, input_w, fc_in_h - 4, 6, FC_INPUT_BG);
    fb_draw_rounded_rect(fc_chat_x, fc_in_y + 2, input_w, fc_in_h - 4, 6, FC_BORDER);

    /* Input text or placeholder */
    if (fc_input_len > 0) {
        char display[FC_INPUT_MAX + 4];
        int di = 0;
        for (int i = 0; i < fc_input_len && i < FC_INPUT_MAX; i++)
            display[di++] = fc_input[i];
        if (fc_cursor_vis && di < FC_INPUT_MAX)
            display[di++] = '_';
        display[di] = 0;
        fb_drawstr_px(fc_chat_x + 8, fc_in_y + (fc_in_h - 16) / 2, display, FC_TEXT, FC_INPUT_BG);
    } else {
        fb_drawstr_px(fc_chat_x + 8, fc_in_y + (fc_in_h - 16) / 2, "Ask me anything...", FC_PLACEHOLDER, FC_INPUT_BG);
        if (fc_cursor_vis) {
            char cursor[2] = { '_', 0 };
            fb_drawstr_px(fc_chat_x + 8, fc_in_y + (fc_in_h - 16) / 2, cursor, FC_LABEL_AI, FC_INPUT_BG);
        }
    }

    /* Send button: right-pointing arrow triangle */
    uint32_t sbg = fc_hover_send ? FC_SEND_HOVER : FC_SEND_COLOR;
    fb_draw_shadow_layered(fc_send_x, fc_send_y, fc_send_sz, fc_send_sz, 8, 90, 4, 4);
    ui_draw_button(fc_send_x, fc_send_y, fc_send_sz, fc_send_sz, sbg, C_SURFACE2, C_TEXT);
    int cx = fc_send_x + fc_send_sz / 2 + 2;
    int cy = fc_send_y + fc_send_sz / 2;
    for (int dy = -7; dy <= 7; dy++) {
        int half = 7 - (dy < 0 ? -dy : dy);
        int lx = cx - half;
        fb_fillrect(lx, cy + dy, half * 2, 1, C_TEXT);
    }
}

int freecode_gui_click(int mx, int my) {
    if (!fc_open) return 0;
    window_t *w = desktop_get_window(fc_win);
    if (!w || !w->visible) return 0;
    fc_compute_layout();
    if (mx < fc_cx || mx >= fc_cx + fc_cw || my < fc_cy || my >= fc_cy + fc_ch) return 0;

    /* Send button */
    if (mx >= fc_send_x && mx < fc_send_x + fc_send_sz &&
        my >= fc_send_y && my < fc_send_y + fc_send_sz) {
        fc_send();
        fc_hover_send = 0;
        return 1;
    }

    /* Focus input on click in input area */
    int input_w = fc_chat_w - fc_send_sz - 6;
    if (mx >= fc_chat_x && mx < fc_chat_x + input_w &&
        my >= fc_in_y + 2 && my < fc_in_y + fc_in_h - 2) {
        desktop_set_focused(fc_win);
        fc_cursor_vis = 1;
        fc_blink_start = timer_get_milliseconds();
        return 1;
    }

    /* Click in chat area focuses window */
    desktop_set_focused(fc_win);
    return 1;
}

void freecode_gui_mousemove(int mx, int my) {
    if (!fc_open) return;
    fc_compute_layout();
    int hover = (mx >= fc_send_x && mx < fc_send_x + fc_send_sz &&
                 my >= fc_send_y && my < fc_send_y + fc_send_sz);
    if (hover != fc_hover_send) {
        fc_hover_send = hover;
        desktop_redraw();
    }
}

void freecode_gui_key(int key) {
    if (!fc_open) return;

    if (key == '\x1b') return;

    if (key == '\n' || key == '\r') {
        fc_send();
        return;
    }

    if (key == '\b' || key == 0x7f) {
        if (fc_input_len > 0) {
            fc_input_len--;
            fc_input[fc_input_len] = 0;
            fc_cursor_vis = 1;
            fc_blink_start = timer_get_milliseconds();
            desktop_redraw();
        }
        return;
    }

    if (key >= ' ' && key < 0x7f && fc_input_len < FC_INPUT_MAX - 1) {
        fc_input[fc_input_len++] = (char)key;
        fc_input[fc_input_len] = 0;
        fc_cursor_vis = 1;
        fc_blink_start = timer_get_milliseconds();
        desktop_redraw();
    }
}

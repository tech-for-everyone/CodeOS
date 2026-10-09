#include "ai_overlay.h"
#include "desktop.h"
#include "windows.h"
#include "ai.h"
#include "string.h"
#include "fb.h"
#include "keyboard.h"
#include "timer.h"

#define AO_BAR_W       520
#define AO_BAR_H       44
#define AO_INPUT_H     36
#define AO_PAD         12
#define AO_MAX_INPUT   200
#define AO_MAX_RESP    1024
#define AO_LINE_H      16
#define AO_RESP_PAD    10

#define AO_BG          0xCC1C1C1E
#define AO_BAR_BG      0xFF2C2C2E
#define AO_INPUT_BG    0xFF1C1C1E
#define AO_BORDER      0x44636366
#define AO_BORDER_GLOW 0x550A84FF
#define AO_TEXT        C_TEXT
#define AO_PLACEHOLDER 0xFF8E8E93
#define AO_ACCENT      C_MAUVE
#define AO_RESP_BG     0xDD2C2C2E

static int ao_open;
static int ao_alpha;           /* 0-255 fade-in */
static uint64_t ao_fade_start;
static char ao_input[AO_MAX_INPUT];
static int ao_input_len;
static int ao_cursor_vis;
static uint64_t ao_blink_start;
static char ao_response[AO_MAX_RESP];
static int ao_has_response;
static int ao_hover_bar;

static int ao_x, ao_y, ao_resp_y, ao_resp_h;

static void ao_compute_layout(int scr_w) {
    ao_x = (scr_w - AO_BAR_W) / 2;
    ao_y = 36;
}

static void ao_wrap_text(const char *text, int max_chars, int *out_lines, int *out_total_h) {
    int lines = 0;
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
        lines++;
        pos = end;
        while (pos < len && text[pos] == ' ') pos++;
    }
    if (lines < 1) lines = 1;
    *out_lines = lines;
    *out_total_h = lines * AO_LINE_H + AO_RESP_PAD * 2;
}

void ai_overlay_init(void) {
    ao_open = 0;
    ao_alpha = 0;
    ao_input_len = 0;
    ao_has_response = 0;
    ao_cursor_vis = 1;
}

int ai_overlay_is_open(void) {
    return ao_open;
}

void ai_overlay_toggle(void) {
    if (ao_open) {
        ao_open = 0;
        return;
    }
    ao_open = 1;
    ao_alpha = 0;
    ao_fade_start = timer_get_milliseconds();
    ao_input_len = 0;
    ao_input[0] = 0;
    ao_cursor_vis = 1;
    ao_blink_start = timer_get_milliseconds();
    ao_has_response = 0;
}

void ai_overlay_draw(int scr_w, int scr_h) {
    if (!ao_open) return;

    /* Fade-in animation */
    uint64_t now = timer_get_milliseconds();
    uint64_t elapsed = now - ao_fade_start;
    if (elapsed < 150) {
        ao_alpha = (int)(elapsed * 255 / 150);
    } else {
        ao_alpha = 255;
    }
    if (ao_alpha > 255) ao_alpha = 255;
    if (ao_alpha < 0) ao_alpha = 0;

    uint8_t alpha = (uint8_t)ao_alpha;

    ao_compute_layout(scr_w);

    /* Full-screen dim overlay */
    fb_fillrect_alpha(0, 0, scr_w, scr_h, 0x000000, (uint8_t)(alpha * 120 / 255));

    /* Bar background with glass morphism */
    int bar_y = ao_y;
    int total_h = AO_BAR_H;
    int resp_lines = 0;
    int resp_h = 0;

    if (ao_has_response) {
        int max_chars = (AO_BAR_W - AO_RESP_PAD * 2) / 8;
        if (max_chars < 10) max_chars = 10;
        ao_wrap_text(ao_response, max_chars, &resp_lines, &resp_h);
        total_h = AO_BAR_H + 6 + resp_h;
    }

    /* Shadow under bar */
    fb_draw_shadow_layered(ao_x, bar_y, AO_BAR_W, total_h, 14, (uint8_t)(alpha * 80 / 255), 8, 6);

    /* Main glass fill */
    fb_fill_rounded_rect(ao_x, bar_y, AO_BAR_W, total_h, 14, 0x00000000 | (((uint32_t)alpha << 24) & 0xFF000000) | (0x1C1C1E & 0x00FFFFFF));

    /* Top highlight hairline */
    fb_fillrect_alpha(ao_x + 14, bar_y + 1, AO_BAR_W - 28, 1, 0xFFFFFF, (uint8_t)(alpha * 20 / 255));

    /* Border */
    uint32_t border = ao_hover_bar ? AO_BORDER_GLOW : (0x00000000 | (((uint32_t)alpha << 24) & 0xFF000000) | (0x636366 & 0x00FFFFFF));
    fb_draw_rounded_rect(ao_x, bar_y, AO_BAR_W, total_h, 14, border);

    /* "FreeCode" label on the left */
    fb_drawstr_px(ao_x + AO_PAD + 2, bar_y + (AO_BAR_H - 14) / 2, "FreeCode", AO_ACCENT, 0);

    /* Vertical separator after label */
    int sep_x = ao_x + AO_PAD + 8 * 8 + 8;
    fb_fillrect(sep_x, bar_y + 8, 1, AO_BAR_H - 16, 0x44636366);

    /* Input field */
    int inp_x = sep_x + 10;
    int inp_y = bar_y + (AO_BAR_H - AO_INPUT_H) / 2;

    if (ao_input_len > 0) {
        char display[AO_MAX_INPUT + 4];
        int di = 0;
        for (int i = 0; i < ao_input_len && i < AO_MAX_INPUT; i++)
            display[di++] = ao_input[i];
        if (ao_cursor_vis && di < AO_MAX_INPUT)
            display[di++] = '|';
        display[di] = 0;
        fb_drawstr_px(inp_x + 8, inp_y + (AO_INPUT_H - 14) / 2, display, AO_TEXT, 0);
    } else {
        fb_drawstr_px(inp_x + 8, inp_y + (AO_INPUT_H - 14) / 2, "Ask FreeCode anything...", AO_PLACEHOLDER, 0);
        if (ao_cursor_vis) {
            fb_drawstr_px(inp_x + 8, inp_y + (AO_INPUT_H - 14) / 2, "|", AO_ACCENT, 0);
        }
    }

    /* Blink cursor */
    if (now - ao_blink_start > 500) {
        ao_cursor_vis = !ao_cursor_vis;
        ao_blink_start = now;
    }

    /* Enter hint on the right */
    fb_drawstr_px(ao_x + AO_BAR_W - AO_PAD - 6 * 8, bar_y + (AO_BAR_H - 12) / 2, "[Enter]", 0x668E8E93, 0);

    /* Response card below bar */
    if (ao_has_response && resp_h > 0) {
        int ry = bar_y + AO_BAR_H + 6;
        int max_chars = (AO_BAR_W - AO_RESP_PAD * 2) / 8;
        if (max_chars < 10) max_chars = 10;

        /* Response background */
        fb_fill_rounded_rect(ao_x, ry, AO_BAR_W, resp_h, 12, 0x00000000 | (((uint32_t)alpha << 24) & 0xFF000000) | (0x2C2C2E & 0x00FFFFFF));
        fb_draw_rounded_rect(ao_x, ry, AO_BAR_W, resp_h, 12, 0x22636366);

        /* "FreeCode" label in response */
        fb_drawstr_px(ao_x + AO_RESP_PAD + 4, ry + AO_RESP_PAD - 2, "FreeCode", AO_ACCENT, 0);

        /* Wrapped response text */
        int text_y = ry + AO_RESP_PAD + AO_LINE_H;
        int pos = 0;
        int len = strlen(ao_response);
        int line_y = text_y;
        while (pos < len) {
            int end = pos + max_chars;
            if (end > len) end = len;
            if (end < len) {
                int bp = end;
                while (bp > pos && ao_response[bp] != ' ') bp--;
                if (bp > pos) end = bp;
            }
            char buf[AO_MAX_RESP];
            int bi = 0;
            for (int i = pos; i < end && bi < AO_MAX_RESP - 1; i++)
                buf[bi++] = ao_response[i];
            buf[bi] = 0;
            fb_drawstr_px(ao_x + AO_RESP_PAD + 4, line_y, buf, AO_TEXT, 0);
            pos = end;
            while (pos < len && ao_response[pos] == ' ') pos++;
            line_y += AO_LINE_H;
        }
    }

    ao_resp_y = bar_y + AO_BAR_H + 6;
    ao_resp_h = resp_h;
}

int ai_overlay_click(int mx, int my) {
    if (!ao_open) return 0;
    int scr_w = fb_getwidth();
    ao_compute_layout(scr_w);

    int total_h = AO_BAR_H;
    if (ao_has_response) total_h = AO_BAR_H + 6 + ao_resp_h;

    /* Click outside = close */
    if (mx < ao_x || mx >= ao_x + AO_BAR_W || my < ao_y || my >= ao_y + total_h) {
        ao_open = 0;
        return 1;
    }

    /* Click in bar = focus */
    ao_hover_bar = 1;
    ao_cursor_vis = 1;
    ao_blink_start = timer_get_milliseconds();
    return 1;
}

void ai_overlay_key(int key) {
    if (!ao_open) return;

    if (key == '\x1b') {
        ao_open = 0;
        return;
    }

    if (key == '\n' || key == '\r') {
        if (ao_input_len == 0) return;
        ao_input[ao_input_len] = 0;
        char resp[AO_MAX_RESP];
        int rlen = ai_query(ao_input, resp, sizeof(resp));
        if (rlen > 0) {
            strlcpy(ao_response, resp, sizeof(ao_response));
            ao_has_response = 1;
        } else {
            strlcpy(ao_response, "I dunno. Try asking about the kernel, GUI, network, or containers.", sizeof(ao_response));
            ao_has_response = 1;
        }
        ao_input_len = 0;
        ao_input[0] = 0;
        ao_cursor_vis = 1;
        ao_blink_start = timer_get_milliseconds();
        return;
    }

    if (key == '\b' || key == 0x7f) {
        if (ao_input_len > 0) {
            ao_input_len--;
            ao_input[ao_input_len] = 0;
            ao_cursor_vis = 1;
            ao_blink_start = timer_get_milliseconds();
        }
        return;
    }

    if (key >= ' ' && key < 0x7f && ao_input_len < AO_MAX_INPUT - 1) {
        ao_input[ao_input_len++] = (char)key;
        ao_input[ao_input_len] = 0;
        ao_cursor_vis = 1;
        ao_blink_start = timer_get_milliseconds();
    }
}

void ai_overlay_mousemove(int mx, int my) {
    if (!ao_open) return;
    int scr_w = fb_getwidth();
    ao_compute_layout(scr_w);
    int hover = (mx >= ao_x && mx < ao_x + AO_BAR_W && my >= ao_y && my < ao_y + AO_BAR_H);
    if (hover != ao_hover_bar) {
        ao_hover_bar = hover;
    }
}

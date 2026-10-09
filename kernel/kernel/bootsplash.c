#include "bootsplash.h"
#include "../arch/x86_64/fb.h"
/* The color palette (C_BASE, C_MAUVE, ...) lives in the canonical panel
 * header; kernel/kernel/windows.h only defines window_t for Zircon and
 * would shadow the unqualified "windows.h" include. */
#include "../../pkgs/core/panels/src/windows.h"
#include "string.h"
#include "pixelman.h"
#include "version.h"
#include "svg.h"
#include "mm.h"

#define SPLASH_DOT_COUNT 5
#define SPLASH_BAR_W 440
#define SPLASH_BAR_H 6
#define SPLASH_COLOR_BG C_BASE

static int splash_active = 0;
static int last_pct = 0;
static int anim_phase = 0;
static char current_msg[64] = "";

static const char codeos_logo_svg[] =
    "<svg viewBox=\"0 0 256 256\">"
    "<defs>"
    "<linearGradient id=\"g1\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">"
    "<stop offset=\"0%\" stop-color=\"#cba6f7\"/>"
    "<stop offset=\"50%\" stop-color=\"#89b4fa\"/>"
    "<stop offset=\"100%\" stop-color=\"#89dceb\"/>"
    "</linearGradient>"
    "</defs>"
    "<path d=\"M128 16 C 64 16 16 64 16 128 C 16 192 64 240 128 240 C 192 240 240 192 240 128 C 240 64 192 16 128 16 Z\" fill=\"url(#g1)\"/>"
    "<path d=\"M 88 80 L 168 80 L 168 96 L 108 96 L 108 118 L 160 118 L 160 134 L 108 134 L 108 176 L 88 176 Z\" fill=\"#1e1e2e\"/>"
    "</svg>";

static void draw_svg_logo(int cx, int cy, int logo_w, uint32_t *buf, int stride, pm_rect_t clip) {
    svg_doc_t *doc = svg_parse(codeos_logo_svg, sizeof(codeos_logo_svg) - 1);
    if (!doc) return;

    int logo_h = (logo_w * doc->vbh) / doc->vbw;
    int rx = cx - logo_w / 2;
    int ry = cy - logo_h / 2;
    int cw = clip.x2 - clip.x1;
    int ch = clip.y2 - clip.y1;

    uint32_t *tmp = malloc(logo_w * logo_h * 4);
    if (tmp) {
        svg_render(doc, tmp, logo_w, logo_h, 0x00000000);
        for (int y = 0; y < logo_h && ry + y < ch; y++) {
            for (int x = 0; x < logo_w && rx + x < cw; x++) {
                uint32_t px = tmp[y * logo_w + x];
                if (px & 0xFF000000) {
                    int fy = ry + y, fx = rx + x;
                    if (fy >= 0 && fx >= 0 && fy < ch && fx < cw)
                        buf[fy * (stride / 4) + fx] = px;
                }
            }
        }
        free(tmp);
    }
    svg_free(doc);
}

static void draw_spinner(int cx, int cy, int t, uint32_t *buf, int stride, pm_rect_t clip) {
    static const int cos256[8] = { 256, 181, 0, -181, -256, -181, 0, 181 };
    static const int sin256[8] = { 0, 181, 256, 181, 0, -181, -256, -181 };
    int r = 12;
    for (int i = 0; i < 8; i++) {
        int phase = (t / 50 + i * 3) % 24;
        int alpha = phase < 12 ? 255 - phase * 20 : (phase - 12) * 20 + 15;
        if (alpha < 20) alpha = 20;
        if (alpha > 255) alpha = 255;
        int px = cx + (r * 7 * cos256[i]) / (10 * 256);
        int py = cy + (r * 7 * sin256[i]) / (10 * 256);
        uint32_t col = (alpha << 24) | 0x0088ccff;
        pm_composite_fill_rounded_rect(buf, stride, clip, px - 3, py - 3, 6, 6, 3, col);
    }
}

static void draw_progress_bar(int pct) {
    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, sw, sh};

    int bar_x = (sw - SPLASH_BAR_W) / 2;
    int bar_y = sh - 90;
    int fill = (SPLASH_BAR_W - 4) * pct / 100;
    if (fill < 0) fill = 0;

    pm_composite_rect(buf, stride, clip, bar_x, bar_y - 24, SPLASH_BAR_W, 30, SPLASH_COLOR_BG);

    pm_composite_fill_rounded_rect(buf, stride, clip, bar_x, bar_y, SPLASH_BAR_W, SPLASH_BAR_H, 3, C_SURFACE0);
    if (fill > 0) {
        uint32_t fill_col = C_MAUVE;
        if (pct > 70) fill_col = C_GREEN;
        else if (pct > 40) fill_col = C_SKY;
        pm_composite_fill_rounded_rect(buf, stride, clip, bar_x + 2, bar_y + 1, fill, SPLASH_BAR_H - 2, 2, fill_col);
    }

    char pct_str[16];
    sprintf(pct_str, "%d%%", pct);
    fb_drawstr_px(bar_x + SPLASH_BAR_W + 14, bar_y - 2, pct_str, C_TEXT, SPLASH_COLOR_BG);

    if (current_msg[0])
        fb_drawstr_px(bar_x, bar_y - 28, current_msg, C_SUBTEXT0, SPLASH_COLOR_BG);
}

static void draw_dots(int active_count, uint32_t *buf, int stride, pm_rect_t clip) {
    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    int dot_r = 4;
    int spacing = 28;
    int start_x = (sw - (SPLASH_DOT_COUNT * spacing)) / 2;
    int dot_y = sh - 118;

    pm_composite_rect(buf, stride, clip, start_x - 10, dot_y - dot_r - 4,
                      SPLASH_DOT_COUNT * spacing + 20, dot_r * 2 + 8, SPLASH_COLOR_BG);

    for (int i = 0; i < SPLASH_DOT_COUNT; i++) {
        int dx = start_x + i * spacing;
        uint32_t col = (i < active_count) ? C_MAUVE : C_SURFACE1;
        pm_composite_fill_rounded_rect(buf, stride, clip,
            dx - dot_r, dot_y - dot_r, dot_r * 2, dot_r * 2, dot_r, col);
    }
}

void bootsplash_init(void) {
    splash_active = 1;
    last_pct = 0;
    anim_phase = 0;
    current_msg[0] = 0;

    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    if (sw == 0 || sh == 0) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, sw, sh};

    pm_composite_rect(buf, stride, clip, 0, 0, sw, sh, SPLASH_COLOR_BG);

    int logo_w = sw < 800 ? 120 : 180;
    draw_svg_logo(sw / 2, sh / 2 - 60, logo_w, buf, stride, clip);

    const char *sub = KERNEL_OS " v" KERNEL_VERSION "  \x97  " KERNEL_ARCH;
    int sub_w = fb_text_width(sub);
    fb_drawstr_px((sw - sub_w) / 2, sh / 2 + 14, sub, C_SUBTEXT0, SPLASH_COLOR_BG);

    pm_composite_rect(buf, stride, clip, (sw - 80) / 2, sh / 2 + 28, 80, 1, C_SURFACE1);

    draw_dots(0, buf, stride, clip);
    draw_progress_bar(0);

    fb_cursor_hide();
}

static uint64_t last_tick = 0;

void bootsplash_tick(void) {
    if (!splash_active) return;
    extern uint64_t timer_get_milliseconds(void);
    uint64_t now = timer_get_milliseconds();
    if (now == 0) return;
    if (now - last_tick < 50) return;
    last_tick = now;
    anim_phase++;

    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    if (sw == 0 || sh == 0) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, sw, sh};

    draw_spinner(sw / 2, sh / 2 + 48, anim_phase, buf, stride, clip);
}

void bootsplash_set_progress(int percent, const char *msg) {
    if (!splash_active) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    last_pct = percent;

    if (msg)
        strncpy_safe(current_msg, msg, sizeof(current_msg));

    int active = (percent * SPLASH_DOT_COUNT) / 100;
    if (active > SPLASH_DOT_COUNT) active = SPLASH_DOT_COUNT;

    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    if (sw == 0 || sh == 0) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, sw, sh};

    draw_dots(active, buf, stride, clip);
    draw_progress_bar(percent);
}

void bootsplash_set_message(const char *msg) {
    if (msg)
        strncpy_safe(current_msg, msg, sizeof(current_msg));
    bootsplash_set_progress(last_pct, current_msg);
}

void bootsplash_finish(void) {
    if (!splash_active) return;
    splash_active = 0;

    fb_cursor_show();

    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    if (sw == 0 || sh == 0) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, sw, sh};
    pm_composite_rect(buf, stride, clip, 0, 0, sw, sh, C_BASE);

    last_pct = 0;
    current_msg[0] = 0;
}

#include "ctxmenu.h"
#include "windows.h"
#include "string.h"
#include "fb.h"

#define CTXMENU_PAD 4

static int ctx_open;
static int ctx_x, ctx_y;
static int ctx_count;
static ctxmenu_item_t ctx_items[CTXMENU_MAX_ITEMS];
static int ctx_hover;
static int ctx_total_h;

void ctxmenu_init(void) {
    ctx_open = 0;
    ctx_hover = -1;
}

void ctxmenu_open(int x, int y, const ctxmenu_item_t *items, int count) {
    if (count > CTXMENU_MAX_ITEMS) count = CTXMENU_MAX_ITEMS;
    ctx_x = x;
    ctx_y = y;
    ctx_count = count;
    ctx_hover = -1;
    for (int i = 0; i < count; i++) ctx_items[i] = items[i];

    ctx_total_h = CTXMENU_PAD;
    for (int i = 0; i < ctx_count; i++) {
        if (ctx_items[i].separator) ctx_total_h += 8;
        else ctx_total_h += CTXMENU_ITEM_H;
    }
    ctx_total_h += CTXMENU_PAD;

    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    if (ctx_x + CTXMENU_W > (int)sw) ctx_x = (int)sw - CTXMENU_W;
    if (ctx_y + ctx_total_h > (int)sh) ctx_y = (int)sh - ctx_total_h;
    if (ctx_x < 0) ctx_x = 0;
    if (ctx_y < 0) ctx_y = 0;

    ctx_open = 1;
}

void ctxmenu_close(void) { ctx_open = 0; }
int  ctxmenu_is_open(void) { return ctx_open; }

void ctxmenu_draw(uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
    if (!ctx_open) return;

    /* Drop shadow — 3-layer soft shadow */
    fb_fill_rounded_rect(ctx_x + 6, ctx_y + 6, CTXMENU_W, ctx_total_h, 10, 0x10000000);
    fb_fill_rounded_rect(ctx_x + 3, ctx_y + 3, CTXMENU_W, ctx_total_h, 10, 0x14000000);
    fb_fill_rounded_rect(ctx_x + 1, ctx_y + 1, CTXMENU_W, ctx_total_h, 10, 0x18000000);
    /* Glass body */
    fb_fill_rounded_rect(ctx_x, ctx_y, CTXMENU_W, ctx_total_h, 10, 0xDD2D2D2D);
    fb_draw_rounded_rect(ctx_x, ctx_y, CTXMENU_W, ctx_total_h, 10, 0x44636366);
    fb_fillrect(ctx_x, ctx_y, CTXMENU_W, 1, 0x22ffffff);
    fb_fillrect_alpha(ctx_x, ctx_y + 1, CTXMENU_W, 1, 0xffffff, 4);

    int iy = ctx_y + CTXMENU_PAD;
    for (int i = 0; i < ctx_count; i++) {
        if (ctx_items[i].separator) {
            fb_fillrect(ctx_x + 12, iy + 3, CTXMENU_W - 24, 1, 0x22808080);
            iy += 8;
        } else {
            if (i == ctx_hover) {
                fb_fill_rounded_rect(ctx_x + 4, iy, CTXMENU_W - 8, CTXMENU_ITEM_H, 6, 0x440A84FF);
            }
            fb_drawstr_px(ctx_x + 16, iy + 4, ctx_items[i].label, C_TEXT, 0);
            iy += CTXMENU_ITEM_H;
        }
    }
}

int ctxmenu_click(int mx, int my) {
    if (!ctx_open) return 0;
    if (mx < ctx_x || mx >= ctx_x + CTXMENU_W || my < ctx_y || my >= ctx_y + ctx_total_h) {
        ctx_open = 0;
        return 1;
    }
    int iy = ctx_y + CTXMENU_PAD;
    for (int i = 0; i < ctx_count; i++) {
        if (ctx_items[i].separator) { iy += 8; continue; }
        if (my >= iy && my < iy + CTXMENU_ITEM_H) {
            ctx_open = 0;
            if (ctx_items[i].action) ctx_items[i].action();
            return 1;
        }
        iy += CTXMENU_ITEM_H;
    }
    ctx_open = 0;
    return 1;
}

void ctxmenu_mousemove(int mx, int my) {
    (void)mx;
    if (!ctx_open) return;
    int iy = ctx_y + CTXMENU_PAD;
    ctx_hover = -1;
    for (int i = 0; i < ctx_count; i++) {
        if (ctx_items[i].separator) { iy += 8; continue; }
        if (my >= iy && my < iy + CTXMENU_ITEM_H) {
            ctx_hover = i;
            return;
        }
        iy += CTXMENU_ITEM_H;
    }
}

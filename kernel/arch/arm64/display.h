#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"

/* Icon identifiers; display_icon() draws each with primitives only. */
enum {
    ICON_TERM, ICON_FOLDER, ICON_GLOBE, ICON_MAIL,
    ICON_CHAT, ICON_PHOTO, ICON_MUSIC, ICON_MAP,
    ICON_CLOCK, ICON_GEAR, ICON_INFO, ICON_BELL,
    ICON_CALC, ICON_BACK,
};

/* Publish the ramfb descriptor and record the surface geometry. */
int display_init(void);

/* Full-screen vertical gradient wallpaper (deep blue -> navy). */
void display_wallpaper(void);

/* Solid rounded rectangle. */
void display_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* One app icon, centred at (cx,cy) with radius r. */
void display_icon(int type, uint32_t cx, uint32_t cy, uint32_t r, uint32_t color);

/* Status bar helpers (called from UI framework). */
void display_draw_status_bar(uint32_t clock, const char *title, int show_back);

/* Draw a small pointer cursor at (x,y). */
void display_draw_pointer(int x, int y);

#endif

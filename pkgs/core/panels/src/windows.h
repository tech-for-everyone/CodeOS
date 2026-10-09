#ifndef WINDOWS_H
#define WINDOWS_H

#include <stdint.h>

#define MAX_WINDOWS     16

/* ── Geometry / chrome ── */
#define CORNER_RADIUS   14
#define TITLEBAR_H      30
#define TRAFFIC_SZ      12
#define TRAFFIC_GAP     8
#define SHADOW_ALPHA    90
#define SHADOW_OFFSET   8

/* ── Shared layout constants (use these instead of magic numbers) ── */
#define WIN_INSET       12      /* horizontal padding from window edge to content */
#define CONTENT_TOP     22      /* vertical offset below titlebar for content start */
#define SECTION_GAP     16      /* gap between sections */
#define CARD_RADIUS     10      /* card/panel corner radius */
#define CARD_PAD        8       /* padding inside a card */
#define BTN_H           24      /* standard button height */
#define TOGGLE_W        36      /* toggle switch width */
#define TOGGLE_H        20      /* toggle switch height */
#define SCROLLBAR_W     6       /* scrollbar track width */

#define EDGE_LEFT   1
#define EDGE_RIGHT  2
#define EDGE_TOP    4
#define EDGE_BOTTOM 8
#define EDGE_SNAP   24

/* ── CodeOS Tahoe palette (macOS Big Sur / dark glass) ── */
#define C_BASE      0xFF1C1C1E   /* window body */
#define C_MANTLE    0xFF2C2C2E   /* elevated surface */
#define C_CRUST     0xFF141416   /* deepest */
#define C_SURFACE0  0xFF2C2C2E
#define C_SURFACE1  0xFF3A3A3C
#define C_SURFACE2  0xFF48484A
#define C_OVERLAY0  0xFF636366
#define C_SUBTEXT0  0xFF8E8E93
#define C_SUBTEXT1  0xFFAEAEB2
#define C_TEXT      0xFFF5F5F7
#define C_LAVENDER  0xFF0A84FF
#define C_BLUE      0xFF0A84FF
#define C_SKY       0xFF64D2FF
#define C_TEAL      0xFF5AC8FA
#define C_GREEN     0xFF30D158
#define C_YELLOW    0xFFFFD60A
#define C_PEACH     0xFFFF9F0A
#define C_MAROON    0xFFFF375F
#define C_RED       0xFFFF453A
#define C_MAUVE     0xFFBF5AF2
#define C_PINK      0xFFFF375F

/* Glass fills (ARGB) */
#define C_GLASS_DARK    0xCC1C1C1E
#define C_GLASS_MID     0xDD2C2C2E
#define C_GLASS_LIGHT   0xEE3A3A3C
#define C_GLASS_BORDER  0x44636366
#define C_GLASS_HI      0x28FFFFFF
#define C_FOCUS_GLOW    0x280A84FF
#define C_ACCENT_BAR    0xFF0A84FF

/* Traffic lights */
#define C_TL_CLOSE  0xFFFF5F57
#define C_TL_MIN    0xFFFEBC2E
#define C_TL_MAX    0xFF28C840

typedef struct {
    int x, y, w, h;
    int orig_x, orig_y, orig_w, orig_h;
    int title_x, title_y, title_w, title_h;
    int close_x, close_y, min_x, min_y, max_x, max_y;
    char title[32];
    uint32_t fg, bg, title_bg;
    int visible;
    int draggable;
    int dragging;
    int drag_off_x, drag_off_y;
    int resizing;
    int resize_edge;
    int has_close;
    int minimized;
    int maximized;
    int snap_zone; /* 0=none, 1=left, 2=right, 3=tl, 4=tr, 5=bl, 6=br, 7=max */
    int snap_x, snap_y, snap_w, snap_h;
} window_t;

void windows_init(void);
int  window_count(void);
window_t *window_get(int idx);
int  window_focused(void);
void window_set_focused(int idx);
void window_focus_next(void);
void window_focus_prev(void);
int  window_new(int x, int y, int w, int h, const char *title, uint32_t fg, uint32_t bg);
void window_close(int idx);
void window_show(int idx, int show);
void window_set_title(int idx, const char *title);
int  window_find_by_title(const char *title);
int  window_raise_or_create(const char *title, int x, int y, int w, int h, uint32_t fg, uint32_t bg);

int  window_handle_click(int mx, int my, int mh, int dh, int scr_w, int scr_h);
void window_handle_drag(int mx, int my, int mh, int dh, int scr_w, int scr_h);
void window_handle_release(void);
void window_draw_decorations(window_t *w, int is_active);
void window_draw(window_t *w, int is_active);
void window_draw_all(int scr_w, int scr_h);
void window_draw_snap_preview(int scr_w, int scr_h);
void window_frame_tick(void);

int  window_menubar_h(void);
int  window_dock_h(void);

/* ── Shared UI component helpers ── */

/* Draw a rounded rectangle button with optional highlight state */
void ui_draw_button(int x, int y, int w, int h,
                    uint32_t bg, uint32_t border, uint32_t text_col);

/* Draw a filled rounded rectangle card (surface + subtle top highlight) */
void ui_draw_card(int x, int y, int w, int h, uint32_t bg);

/* Draw a horizontal progress bar (track + fill) */
void ui_draw_progress(int x, int y, int w, int h,
                      int pct, uint32_t track, uint32_t fill);

/* Draw a toggle switch (on/off) */
void ui_draw_toggle(int x, int y, int on, uint32_t accent);

/* Draw a scrollbar track with thumb indicator */
void ui_draw_scrollbar(int x, int y, int h,
                       int total, int visible, int offset);

/* Draw a section header label */
void ui_draw_section(int x, int y, int w, const char *title);

#endif

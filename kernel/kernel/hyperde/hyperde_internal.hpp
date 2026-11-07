#ifndef HYPERDE_INTERNAL_HPP
#define HYPERDE_INTERNAL_HPP
/* Internal shared declarations for HyperDE C++ port (was Rust hyperde_shell). */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Kernel C ABI */
typedef int c_int;
struct RtcTime { c_int second, minute, hour, day, month, year; };

void kprintf(const char *fmt, ...);
uint32_t fb_getwidth(void);
uint32_t fb_getheight(void);
c_int fb_get_pitch(void);
uint8_t fb_get_bpp(void);
uint32_t *fb_get_active_buffer(void);
void rtc_read(struct RtcTime *t);
uint64_t timer_get_milliseconds(void);
uint64_t sched_busy_ticks(void);
uint64_t pmm_count_used(void);
uint64_t pmm_total_pages(void);
int lvgl_wm_window_count(const void *wm);
const void *lvgl_wm_window_at(const void *wm, int idx);

/* lvgl window layout (mirrors Rust LvglWindow / kernel WM) */
typedef struct { c_int x, y, w, h; } LvglRect;
typedef struct {
    c_int id;
    uint8_t title[128];
    LvglRect rect;
    void *obj;
    c_int visible;
    c_int focused;
} LvglWindow;

/* penrose bridge (X11 desktop windows) */
typedef struct {
    uint32_t xid;
    int16_t x, y, w, h;
    uint8_t mapped;
    uint8_t focused;
    char title[40];
} PrsDesktopWin;
int prs_desktop_windows(PrsDesktopWin *buf, int max);

#ifdef __cplusplus
}
#endif

#define PRS_WIN_MAX 32
#define RELAX __ATOMIC_RELAXED
#define ACQREL __ATOMIC_ACQ_REL

struct Palette {
    uint32_t accent, green, text, sub;
    uint32_t glass_top, glass_bot, glass_alpha;
    uint32_t focused_border, normal_border, border_width, bar_h;
};

extern "C" const Palette *hyperde_pal(void);
extern "C" void hyperde_palette_load_config(void);
extern "C" uint32_t hyperde_scancode_to_key_pub(uint32_t sc);

/* shared primitives implemented in hyperde_shell.cpp */
void hyperde_glass_px(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t x, int64_t y, uint32_t g, uint32_t alpha);
void hyperde_fill_circle(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t cx, int64_t cy, int64_t r, uint32_t c, uint32_t alpha);
void hyperde_round_rect(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t r, uint32_t c, uint32_t alpha);
int64_t hyperde_draw_text(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t x, int64_t y0, const uint8_t *s, size_t n, uint32_t color, int64_t scale, uint32_t alpha);

#endif

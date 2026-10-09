#include "clock.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"
#include "fb.h"
#include "rtc.h"

#define CLOCK_W 260
#define CLOCK_H 180

static int cl_open, cl_win;

void clock_open(void) {
    if (cl_open) return;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    cl_win = desktop_new_window((sw - CLOCK_W) / 2, (sh - CLOCK_H) / 3, CLOCK_W, CLOCK_H, "Clock", 0xFFFFFFFF, C_BASE);
    if (cl_win < 0) return;
    cl_open = 1;
    desktop_redraw();
}

int clock_is_open(void) { return cl_open; }
void clock_close(void) { cl_open = 0; }

void clock_draw(void) {
    if (!cl_open) return;
    window_t *w = desktop_get_window(cl_win);
    if (!w || !w->visible) return;
    int cx = w->x + 4, cy = w->y + 22, cw = w->w - 8, ch = w->h - 26;
    if (cw < 20 || ch < 20) return;

    fb_fillrect(cx, cy, cw, ch, C_MANTLE);

    rtc_time_t t;
    rtc_read(&t);

    char time_str[16];
    int n = 0;
    time_str[n++] = '0' + t.hour / 10;
    time_str[n++] = '0' + t.hour % 10;
    time_str[n++] = ':';
    time_str[n++] = '0' + t.minute / 10;
    time_str[n++] = '0' + t.minute % 10;
    time_str[n++] = ':';
    time_str[n++] = '0' + t.second / 10;
    time_str[n++] = '0' + t.second % 10;
    time_str[n] = 0;

    int tw = n * 16;
    int tx = cx + (cw - tw) / 2;
    int ty = cy + 24;

    for (int i = 0; time_str[i]; i++) {
        fb_drawchar_px(tx + i * 16, ty, time_str[i], C_TEXT, C_MANTLE);
    }

    static const char *months[] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    static const char *days[] = {
        "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
    };

    int mi = t.month - 1;
    if (mi < 0) mi = 0;
    if (mi > 11) mi = 11;

    int dow = 0;
    if (t.year >= 0) {
        int y = t.year, m = t.month, d = t.day;
        if (m < 3) { m += 12; y--; }
        dow = (d + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    }
    int di = dow;
    if (di < 0) di = 0;
    if (di > 6) di = 6;

    char date_buf[64];
    n = 0;
    const char *ds = days[di];
    while (*ds && n < 63) date_buf[n++] = *ds++;
    date_buf[n++] = ',';
    date_buf[n++] = ' ';
    const char *ms = months[mi];
    while (*ms && n < 63) date_buf[n++] = *ms++;
    date_buf[n++] = ' ';
    date_buf[n++] = '0' + t.day / 10;
    date_buf[n++] = '0' + t.day % 10;
    date_buf[n++] = ',';
    date_buf[n++] = ' ';
    int yr = t.year;
    if (yr < 100) yr += 2000;
    date_buf[n++] = '0' + (yr / 1000) % 10;
    date_buf[n++] = '0' + (yr / 100) % 10;
    date_buf[n++] = '0' + (yr / 10) % 10;
    date_buf[n++] = '0' + yr % 10;
    date_buf[n] = 0;

    int dw = n * 8;
    int dx = cx + (cw - dw) / 2;
    fb_drawstr_px(dx, ty + 32, date_buf, C_SUBTEXT0, C_MANTLE);
}

int clock_click(int mx, int my) {
    (void)mx; (void)my;
    return 0;
}
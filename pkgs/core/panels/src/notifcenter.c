extern "C" {
#include "notifcenter.h"
#include "wm.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
}

#define NOTIF_MAX 8
#define NOTIF_H 64
#define NOTIF_W 280

static struct {
    char text[NOTIF_TEXT_MAX];
    int time_ms;
    bool is_read;
} notifs[NOTIF_MAX];
static int notif_count = 0;
static int notif_scroll = 0;

void notifcenter_init(void) {
    notif_count = 0;
    notif_scroll = 0;
}

void notifcenter_toggle(void) {
}

void notifcenter_add(const char *text, bool unread) {
    if (notif_count < NOTIF_MAX) {
        strncpy(notifs[notif_count].text, text, NOTIF_TEXT_MAX - 1);
        notifs[notif_count].text[NOTIF_TEXT_MAX - 1] = 0;
        notifs[notif_count].is_read = !unread;
        notif_count++;
    }
    if (notif_count > NOTIF_MAX) {
        memmove(notifs, notifs + 1, (NOTIF_MAX - 1) * sizeof(notifs[0]));
        notif_count = NOTIF_MAX;
    }
}

int notifcenter_is_open(void) {
    return 1;
}

void notifcenter_draw(uint32_t scr_w, uint32_t scr_h) {
    int y = 80;
    int x = (scr_w - NOTIF_W) / 2;

    /* Glass panel */
    fb_fillrect_gradient_v(x, y, NOTIF_W, NOTIF_H,
                           0x30252525, 0x20151515);

    /* Title */
    fb_drawstr_px(x + 8, y + 6, "Notifications", C_BLUE, 0);

    /* Divider */
    fb_fillrect(x + 4, y + 28, NOTIF_W - 8, 1, 0x30FFFFFF);

    /* Show notifications */
    int start = notif_scroll;
    for (int i = 0; i < NOTIF_MAX && start + i < notif_count; i++) {
        int ni = start + i;
        int iy = y + 32 + (i * 24);

        if (ni >= notif_count) break;

        /* Unread dot */
        if (!notifs[ni].is_read) {
            fb_fill_rounded_rect(x + 6, iy + 4, 8, 8, 3, 0xE0FF0000);
        }

        /* Text */
        fb_drawstr_px(x + 20, iy + 2, notifs[ni].text, C_TEXT, 0);

        /* Close button */
        fb_fill_rounded_rect(x + NOTIF_W - 24, iy + 2, 16, 16, 3, 0x30000000);
        fb_drawstr_px(x + NOTIF_W - 18, iy + 1, "×", 0x800000, 0);

        if (iy > scr_h - 30) break;
    }

    /* Footer */
    fb_drawstr_px(x + 8, y + NOTIF_H - 20, "Clear all", C_DIM, 0);
}

int notifcenter_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    int y = 80;
    int x = (scr_w - NOTIF_W) / 2;

    /* Check close buttons */
    for (int i = 0; i < NOTIF_MAX && start + i < notif_count; i++) {
        int ni = start + i;
        int iy = y + 32 + (i * 24);
        if (mx >= x + NOTIF_W - 24 && mx < x + NOTIF_W - 8 &&
            my >= iy && my < iy + 16) {
            return 1;
        }
    }

    /* Check clear all */
    if (my >= y + NOTIF_H - 20 && mx >= x + 8 && mx < x + NOTIF_W - 8) {
        notif_count = 0;
        return 1;
    }

    return 0;
}

void notifcenter_tick(void) {
}

void notifcenter_mousemove(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
}
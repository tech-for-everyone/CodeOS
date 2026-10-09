extern "C" {
#include "menubar.h"
#include "notifcenter.h"
#include "wm.h"
#include "windows.h"
#include "string.h"
#include "fb.h"
#include "io.h"
#include "rtc.h"
#include "timer.h"
}

extern "C" int net_ready(void);
extern "C" int clamav_sig_count(void);
extern "C" void about_open(void);
extern "C" void settings_open(void);
extern "C" void sysmon_open(void);

/* ── New simplified menubar ── */
#define MENUBAR_H 30
#define MENUBAR_GLASS_HEIGHT 28

void menubar_init(void) {
    /* Initialize menubar state */
}

int menubar_h(void) {
    return wm_menubar_enabled() ? MENUBAR_H : 0;
}

void menubar_draw(uint32_t scr_w, uint32_t scr_h, int active_win, const char *active_title) {
    (void)active_win; (void)active_title;

    if (!wm_menubar_enabled()) return;

    /* New minimalist glass menubar */
    fb_fillrect_gradient_v(0, 0, scr_w, MENUBAR_H,
                           0x20F0F0F0, 0x10E0E0E0);

    /* Accent bar */
    fb_fillrect(0, MENUBAR_H - 2, scr_w, 2, 0x40FFFFFF);

    /* Left divider */
    fb_fillrect(0, 0, 2, MENUBAR_H, 0x30000000);

    /* Right divider */
    fb_fillrect(scr_w - 2, 0, 2, MENUBAR_H, 0x30000000);

    /* Center - system info */
    int center_x = scr_w / 2;

    /* Workspace indicators */
    int ws_left = 20;
    int ws_count = wm_workspace_count();
    int ws_active = wm_active_workspace();

    for (int i = 0; i < ws_count && i < 8; i++) {
        int xi = center_x - (ws_count * 14 / 2) + (i * 14);
        if (i == ws_active) {
            /* Active workspace */
            fb_fill_rounded_rect(xi, 4, 10, 10, 4, 0x400066FF);
        } else {
            fb_fill_rounded_rect(xi, 4, 10, 10, 4, 0x30808080);
        }
    }

    /* Notification center toggle on right */
    int right_x = scr_w - 40;
    fb_fill_rounded_rect(right_x, 6, 24, 16, 4, 0x30FFFFFF);
    fb_drawstr_px(right_x + 4, 1, "?", C_DIM, 0);
}

/* Simplified click handling */
int menubar_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_h;
    if (my < 0 || my >= MENUBAR_H) return 0;

    /* Click on right area toggles notification center */
    int right_x = scr_w - 40;
    if (mx >= right_x && mx < right_x + 24) {
        notifcenter_toggle();
        return 1;
    }

    /* Click on workspace indicators */
    int ws_count = wm_workspace_count();
    int start_x = (scr_w - ws_count * 14) / 2;
    for (int i = 0; i < ws_count; i++) {
        int dx = start_x + i * 14;
        if (mx >= dx && mx < dx + 14) {
            wm_handle_key('1' + i);
            return 1;
        }
    }

    return 0;
}

int menubar_should_toggle_launcher(void) {
    return 0;
}

void menubar_draw_apple_menu(uint32_t scr_w) {
    (void)scr_w;
    /* No Apple menu in simplified version */
}

int menubar_apple_menu_click(int mx, int my) {
    (void)mx; (void)my;
    return 0;
}
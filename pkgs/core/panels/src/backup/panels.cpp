extern "C" {
#include "panels.h"
#include "menubar.h"
#include "dock.h"
#include "launcher.h"
#include "windows.h"
#include "wm.h"
#include "terminal.h"
#include "about.h"
#include "devstore.h"
#include "settings.h"
#include "openweb.h"
#include "fmanager.h"
#include "calc.h"
#include "appvm.h"
#include "freecode_gui.h"
#include "sysmon.h"
#include "desktop.h"
#include "string.h"
#include "mm.h"
#include "fb.h"
#include "timer.h"
#include "mouse.h"
#include "input.h"
#include "keyboard.h"
#include "kprintf.h"
#include "net.h"
#include "ctxmenu.h"
#include "notifcenter.h"
#include "wallpaper.h"
#include "updater.h"
#include "switcher.h"
#include "installer.h"
#include "clamav.h"
#include "rtc.h"
#include "launchpad.h"
#include "ai_overlay.h"
#include "firewall.h"
#include "examples/info.h"
#include "jengine/jengine.h"
}

#define DECL_ICON_64(n) extern const unsigned char _binary_icon_ ## n ## _rgba[]; extern const int _binary_icon_ ## n ## _rgba_len
#define DECL_ICON_44(n) extern const unsigned char _binary_icon_ ## n ## _44_rgba[]; extern const int _binary_icon_ ## n ## _44_rgba_len

DECL_ICON_64(terminal);   DECL_ICON_44(terminal);
DECL_ICON_64(about);      DECL_ICON_44(about);
DECL_ICON_64(calculator); DECL_ICON_44(calculator);
DECL_ICON_64(store);      DECL_ICON_44(store);
DECL_ICON_64(settings);   DECL_ICON_44(settings);
DECL_ICON_64(openweb);    DECL_ICON_44(openweb);
DECL_ICON_64(files);      DECL_ICON_44(files);
DECL_ICON_64(exit);       DECL_ICON_64(exit);
DECL_ICON_64(trash);      DECL_ICON_44(trash);
DECL_ICON_64(appvm);      DECL_ICON_44(appvm);
DECL_ICON_64(freecode);   DECL_ICON_44(freecode);
DECL_ICON_64(sysinfo);    DECL_ICON_44(sysinfo);
DECL_ICON_64(sysmon);     DECL_ICON_44(sysmon);
DECL_ICON_64(firewall);   DECL_ICON_64(firewall);

static const unsigned char *icon_data_64[APP_COUNT_MAX] = {
    _binary_icon_terminal_rgba, _binary_icon_about_rgba, _binary_icon_calculator_rgba,
    _binary_icon_store_rgba, _binary_icon_settings_rgba, _binary_icon_openweb_rgba,
    _binary_icon_files_rgba, _binary_icon_exit_rgba, _binary_icon_freecode_rgba,
    _binary_icon_sysinfo_rgba, _binary_icon_sysmon_rgba,
    _binary_icon_firewall_rgba
};

static const unsigned char *icon_data_44[APP_COUNT_MAX] = {
    _binary_icon_terminal_44_rgba, _binary_icon_about_44_rgba, _binary_icon_calculator_44_rgba,
    _binary_icon_store_44_rgba, _binary_icon_settings_44_rgba, _binary_icon_openweb_44_rgba,
    _binary_icon_files_44_rgba, _binary_icon_exit_44_rgba, _binary_icon_freecode_44_rgba,
    _binary_icon_sysinfo_44_rgba, _binary_icon_sysmon_44_rgba,
    _binary_icon_firewall_44_rgba
};

const char *app_names[APP_COUNT_MAX] = {
    "Terminal", "About", "Calc", "DevStore", "Settings", "OpenWeb", "Explorer", "Exit", "FreeCode",
    "Sys Info", "SysMon", "Firewall", "Steam"
};

int app_is_system[APP_COUNT_MAX] = {
    1, 1, 0, 1, 1, 0, 0, 1, 1,
    1, 1, 1, 0
};

uint32_t *icon_buf_dock[APP_COUNT_MAX];
uint32_t *icon_buf_launch[APP_COUNT_MAX];
uint32_t *trash_buf_dock;

static uint32_t *rgba_to_buf(const unsigned char *rgba, int len, int sz) {
    if (!rgba || len != sz * sz * 4) return 0;
    uint32_t *buf = (uint32_t*)malloc(sz * sz * 4);
    if (!buf) return 0;
    for (int i = 0; i < sz * sz; i++) {
        unsigned char r = rgba[i * 4], g = rgba[i * 4 + 1];
        unsigned char b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
        buf[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    return buf;
}

static void launch_app(int item) {
    switch (item) {
        case 0: terminal_open(); break;
        case 1: about_open(); break;
        case 2: calc_open(); break;
        case 3: if (!devstore_is_open()) devstore_open(); break;
        case 4: settings_open(); break;
        case 5: openweb_open(); break;
        case 6: fmanager_open(); break;
        case 7: panels_desktop_stop_with_reason(PANELS_STOP_EXIT); break;
        case 8: freecode_gui_open(); break;
        case 9: sys_info_open(); break;
        case 10: sysmon_open(); break;
        case 11: firewall_open(); break;
    }
}

int panels_init(void) {
    menubar_init();
    dock_init();
    launcher_init();
    launchpad_init();
    ai_overlay_init();
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        icon_buf_dock[i] = rgba_to_buf(icon_data_44[i], ICON_SZ_DOCK * ICON_SZ_DOCK * 4, ICON_SZ_DOCK);
        icon_buf_launch[i] = rgba_to_buf(icon_data_64[i], ICON_SZ_LAUNCH * ICON_SZ_LAUNCH * 4, ICON_SZ_LAUNCH);
    }
    trash_buf_dock = rgba_to_buf(_binary_icon_trash_44_rgba, ICON_SZ_DOCK * ICON_SZ_DOCK * 4, ICON_SZ_DOCK);
    jengine_init();
    return 1;
}

int panels_menubar_h(void) { return menubar_h(); }
int panels_dock_h(void) { return dock_h(); }

void panels_draw_menubar(uint32_t scr_w, uint32_t scr_h, int active_win, const char *active_title) {
    menubar_draw(scr_w, scr_h, active_win, active_title);
}

void panels_draw_dock(uint32_t scr_w, uint32_t scr_h) {
    dock_draw(scr_w, scr_h);
}

void panels_draw_launcher(uint32_t scr_w, uint32_t scr_h) {
    launcher_draw(scr_w, scr_h);
}

void panels_draw_all(uint32_t scr_w, uint32_t scr_h, int active_win, const char *active_title) {
    menubar_draw(scr_w, scr_h, active_win, active_title);
    dock_draw(scr_w, scr_h);
    launcher_draw(scr_w, scr_h);
}

int panels_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    if (ai_overlay_is_open()) {
        if (ai_overlay_click(mx, my)) { panels_desktop_redraw(); return 1; }
    }
    if (launchpad_is_open()) {
        int pending = launchpad_update(timer_get_milliseconds());
        if (pending >= 0 && pending < APP_COUNT_MAX) {
            launchpad_close();
            launch_app(pending);
            return 1;
        }
        int item = launchpad_click(mx, my, scr_w, scr_h);
        if (item >= 0 && item < APP_COUNT_MAX) {
            launchpad_close();
            launch_app(item);
            return 1;
        }
        return 1;
    }
    if (launcher_is_open()) {
        int pending = launcher_update(timer_get_milliseconds());
        if (pending >= 0 && pending < APP_COUNT_MAX) {
            launcher_close();
            launch_app(pending);
            return 1;
        }
        int item = launcher_click(mx, my, scr_w, scr_h);
        if (item >= 0 && item < APP_COUNT_MAX) {
            return 1;
        }
        launcher_close();
        return 1;
    }
    int dock_item = dock_click(mx, my, scr_w, scr_h);
    if (dock_item >= 0 && dock_item < APP_COUNT_MAX) {
        launch_app(dock_item);
        return 1;
    }
    if (dock_item == APP_COUNT_MAX)
        return 1;
    if (menubar_click(mx, my, scr_w, scr_h)) {
        if (menubar_should_toggle_launcher())
            launcher_toggle();
        return 1;
    }
    return 0;
}

int panels_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    int changed = 0;
    if (dock_hover(mx, my, scr_w, scr_h)) changed = 1;
    if (launchpad_is_open()) {
        if (launchpad_hover(mx, my, scr_w, scr_h)) changed = 1;
    } else {
        if (launcher_hover(mx, my, scr_w, scr_h)) changed = 1;
    }
    return changed;
}

void panels_toggle_start(void) {
    launcher_toggle();
}

void panels_anim_update(uint64_t now_ms) {
    if (launchpad_is_open()) {
        int pending = launchpad_update(now_ms);
        if (pending >= 0 && pending < APP_COUNT_MAX) {
            launchpad_close();
            launch_app(pending);
        }
    } else if (launcher_is_open()) {
        int pending = launcher_update(now_ms);
        if (pending >= 0 && pending < APP_COUNT_MAX) {
            launcher_close();
            launch_app(pending);
        }
    }
    dock_tick(0);
}

int panels_jengine_eval(const char *source, char *output, int max_out) {
    jengine_value result = jengine_eval(source);
    if (result.type == 0) { /* number */
        int n = snprintf(output, max_out, "%lld", (long long)result.num_val);
        return n;
    } else if (result.type == 1) { /* string */
        int n = snprintf(output, max_out, "%s", result.str_val ? result.str_val : "");
        return n;
    } else if (result.type == 4) { /* undefined */
        int n = snprintf(output, max_out, "undefined");
        return n;
    }
    snprintf(output, max_out, "error");
    return 5;
}
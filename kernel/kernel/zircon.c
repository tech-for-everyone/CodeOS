/* Zircon — the Better Android
 * Kernel-side app manager and desktop enhancements for CodeOS.
 * Adds Android-style app drawer, notification panel, and app lifecycle. */

#include "zircon.h"
#include "windows.h"
#include "panels.h"
#include "kprintf.h"
#include "string.h"
#include "zdm.h"
#include "../drivers/audio.h"
#include "../drivers/zircon_ipc.h"
#include "../arch/x86_64/fb.h"
#include "../drivers/timer.h"
#include "pixelman.h"
#include "process.h"
#include "elf.h"
#include "shell.h"
#include "umode.h"
#include "sched.h"

/* ── Notification system ── */

#define NOTIF_MAX 8
#define NOTIF_HISTORY 16
#define NOTIF_W 320
#define NOTIF_H 80
#define NOTIF_DURATION 5000

typedef struct {
    char title[48];
    char text[128];
    uint64_t time;
    uint64_t expiry;
    int active;
    int dismissed;
} notif_t;

static notif_t notifs[NOTIF_MAX];
static int notif_count;
static notif_t notif_history[NOTIF_HISTORY];
static int notif_history_count;
static int shade_open;
static int hist_scroll;
static int hist_item_h = 48;

static const char *reltime(uint64_t then, uint64_t now) {
    uint64_t delta = (now > then) ? (now - then) / 1000 : 0;
    if (delta < 60)      return "now";
    if (delta < 3600)    return "1m ago";  /* don't bother with arbitrary minutes */
    if (delta < 7200)    return "1h ago";
    if (delta < 86400)   return "2h ago";
    return "older";
}

void zircon_notify(const char *title, const char *text) {
    if (notif_count >= NOTIF_MAX) {
        for (int i = 1; i < NOTIF_MAX; i++)
            notifs[i - 1] = notifs[i];
        notif_count--;
    }
    int i = notif_count++;
    notifs[i].active = 1;
    notifs[i].dismissed = 0;
    notifs[i].expiry = timer_get_milliseconds() + NOTIF_DURATION;
    notifs[i].time = timer_get_milliseconds();
    int j = 0;
    if (title)
        while (title[j] && j < 47) { notifs[i].title[j] = title[j]; j++; }
    notifs[i].title[j] = 0;
    j = 0;
    if (text)
        while (text[j] && j < 127) { notifs[i].text[j] = text[j]; j++; }
    notifs[i].text[j] = 0;

    audio_notify_beep();

    /* Send to userspace via IPC */
    {
        zircon_ipc_msg_t ipc_msg;
        ipc_msg.type = ZIRCON_IPC_NOTIFY;
        ipc_msg.x = 0; ipc_msg.y = 0;
        int k = 0;
        if (title)
            while (title[k] && k < 127) { ipc_msg.text[k] = title[k]; k++; }
        ipc_msg.text[k++] = ':';
        ipc_msg.text[k++] = ' ';
        if (text) {
            int ti = 0;
            while (text[ti] && k < 127) { ipc_msg.text[k++] = text[ti++]; }
        }
        ipc_msg.text[k < 128 ? k : 127] = 0;
        zircon_ipc_send(&ipc_msg);
    }

    /* Archive to history */
    if (notif_history_count < NOTIF_HISTORY)
        notif_history[notif_history_count++] = notifs[i];

    zdm_log(ZDM_NOTIFICATION, notifs[i].title);
}

static void dismiss_notif(int idx) {
    if (idx < 0 || idx >= notif_count) return;
    notifs[idx].active = 0;
    notifs[idx].dismissed = 1;
}

static void dismiss_all(void) {
    for (int i = 0; i < notif_count; i++) {
        notifs[i].active = 0;
        notifs[i].dismissed = 1;
    }
}

static void dismiss_history(int idx) {
    if (idx < 0 || idx >= notif_history_count) return;
    for (int i = idx + 1; i < notif_history_count; i++)
        notif_history[i - 1] = notif_history[i];
    notif_history_count--;
}

static void clear_history(void) {
    notif_history_count = 0;
    hist_scroll = 0;
}

static void archive_expired(void) {
    uint64_t now = timer_get_milliseconds();
    for (int i = 0; i < notif_count; i++) {
        if (notifs[i].active && !notifs[i].dismissed && now >= notifs[i].expiry) {
            notifs[i].active = 0;
            if (notif_history_count < NOTIF_HISTORY)
                notif_history[notif_history_count++] = notifs[i];
        }
    }
}

/* ── Notification toast (top-right) ── */

static void notif_draw(void) {
    archive_expired();
    uint64_t now = timer_get_milliseconds();
    int x = (int)fb_getwidth() - NOTIF_W - 10;
    int y = 32;
    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, (int)fb_getwidth(), (int)fb_getheight()};

    for (int i = 0; i < notif_count; i++) {
        if (!notifs[i].active || notifs[i].dismissed) continue;
        if (now >= notifs[i].expiry) {
            notifs[i].active = 0;
            if (notif_history_count < NOTIF_HISTORY)
                notif_history[notif_history_count++] = notifs[i];
            continue;
        }
        pm_composite_fill_rounded_rect(buf, stride, clip,
            x, y, NOTIF_W, NOTIF_H, 8, 0xDD313244);
        fb_drawstr_px(x + 10, y + 8, notifs[i].title, C_TEXT, 0);
        fb_drawstr_px(x + 10, y + 30, notifs[i].text, C_SUBTEXT0, 0);
        y += NOTIF_H + 6;
    }
}

/* ── Notification shade (pull-down) ── */

void zircon_toggle_shade(void) {
    shade_open = !shade_open;
    zdm_log(ZDM_SHADE_TOGGLE, shade_open ? "open" : "close");
}

int zircon_shade_open(void) {
    return shade_open;
}

static void shade_draw(void) {
    if (!shade_open) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    pm_rect_t clip = {0, 0, sw, sh};
    uint64_t now = timer_get_milliseconds();

    int sx = 0;
    int sy = panels_menubar_h();

    /* Semi-transparent overlay */
    pm_composite_rect_alpha(buf, stride, clip, sx, sy, sw, sh, 0x000000, 204);

    /* ── Header ── */
    int header_y = sy + 8;
    int hdr_h = 36;
    pm_composite_rect(buf, stride, clip, sx, header_y, sw, hdr_h, 0xCC313244);
    fb_drawstr_px(sx + 16, header_y + 8, "Notifications", C_TEXT, 0);
    if (notif_count > 0 || notif_history_count > 0)
        fb_drawstr_px(sx + sw - 80, header_y + 8, "Clear all", C_SUBTEXT0, 0);

    /* ── Quick toggles ── */
    int toggle_y = header_y + hdr_h + 6;
    pm_composite_rect(buf, stride, clip, sx + 8, toggle_y, sw - 16, 36, 0x44313244);
    fb_drawstr_px(sx + 20, toggle_y + 8, "Wi-Fi  Bluetooth  DND  Dark Mode", C_TEXT, 0);

    /* ── Active notification list ── */
    int list_y = toggle_y + 44;
    int item_h = 56;
    int visible = 0;
    for (int i = 0; i < notif_count && list_y + item_h < sh - 40; i++) {
        if (notifs[i].dismissed) continue;
        pm_composite_rect(buf, stride, clip, sx + 8, list_y, sw - 16, item_h - 4, 0x33313244);
        fb_drawstr_px(sx + 20, list_y + 6, notifs[i].title, C_TEXT, 0);
        fb_drawstr_px(sx + 20, list_y + 24, notifs[i].text, C_SUBTEXT0, 0);
        fb_drawstr_px(sx + sw - 40, list_y + 6, reltime(notifs[i].time, now), C_SUBTEXT0, 0);
        list_y += item_h;
        visible++;
    }
    if (visible == 0 && notif_history_count == 0) {
        fb_drawstr_px(sx + 20, list_y + 8, "No notifications", C_SUBTEXT0, 0);
    }

    /* ── History section ── */
    list_y += 4;
    if (notif_history_count > 0) {
        /* Section header with count + clear button */
        pm_composite_rect(buf, stride, clip, sx, list_y, sw, 28, 0x1A1A2E44);
        char hbuf[32];
        int kz = 0;
        hbuf[kz++] = 'R';
        hbuf[kz++] = 'e';
        hbuf[kz++] = 'c';
        hbuf[kz++] = 'e';
        hbuf[kz++] = 'n';
        hbuf[kz++] = 't';
        hbuf[kz++] = ' ';
        if (notif_history_count < 10) { hbuf[kz++] = '0' + notif_history_count; }
        else { hbuf[kz++] = '0' + notif_history_count / 10; hbuf[kz++] = '0' + notif_history_count % 10; }
        hbuf[kz] = 0;
        fb_drawstr_px(sx + 16, list_y + 6, hbuf, C_SUBTEXT0, 0);
        fb_drawstr_px(sx + sw - 72, list_y + 6, "Clear", 0xFF666666, 0);
        list_y += 30;

        int max_visible = (sh - 40 - list_y) / hist_item_h;
        if (max_visible < 1) max_visible = 1;
        int total = notif_history_count;
        int start = total - max_visible - hist_scroll;
        if (start < 0) start = 0;
        int end = total - hist_scroll;
        if (end > total) end = total;
        if (end - start > max_visible) end = start + max_visible;

        for (int i = start; i < end && list_y + hist_item_h < sh - 16; i++) {
            int hy = list_y;
            /* Dimmer background for history items */
            pm_composite_rect(buf, stride, clip, sx + 8, hy, sw - 16, hist_item_h - 2, 0x22222244);
            /* App color dot */
            pm_composite_rect(buf, stride, clip, sx + 18, hy + 8, 6, 6, C_SKY);
            /* Title */
            fb_drawstr_px(sx + 32, hy + 4, notif_history[i].title, C_SUBTEXT0, 0);
            /* Time */
            fb_drawstr_px(sx + sw - 80, hy + 4, reltime(notif_history[i].time, now), 0xFF555555, 0);
            /* Text */
            fb_drawstr_px(sx + 32, hy + 22, notif_history[i].text, 0xFF666666, 0);
            /* Dismiss '×' button */
            fb_drawstr_px(sx + sw - 28, hy + 4, "×", 0xFF555555, 0);
            list_y += hist_item_h;
        }

        /* Scroll indicator */
        int remaining = total - (end - start);
        if (remaining > 0) {
            char sibuf[16];
            int si = 0;
            if (remaining < 10) { sibuf[si++] = '0' + remaining; }
            else { sibuf[si++] = '0' + remaining / 10; sibuf[si++] = '0' + remaining % 10; }
            sibuf[si++] = ' '; sibuf[si++] = 'm'; sibuf[si++] = 'o'; sibuf[si++] = 'r'; sibuf[si++] = 'e';
            sibuf[si] = 0;
            fb_drawstr_px(sx + 20, list_y + 4, sibuf, C_SUBTEXT0, 0);
        }
    }
}

static int shade_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    if (!shade_open) return 0;

    /* Click outside shade → close */
    if (my < panels_menubar_h()) {
        shade_open = 0;
        return 1;
    }

    int mh = panels_menubar_h();
    int header_y = mh + 8;
    int hdr_h = 36;

    /* Check "Clear all" in header */
    if (my >= header_y && my < header_y + hdr_h) {
        if (mx >= (int)scr_w - 90 && mx < (int)scr_w - 8) {
            dismiss_all();
            return 1;
        }
        return 0;
    }

    int toggle_y = header_y + hdr_h + 6;
    int list_y = toggle_y + 44;
    int item_h = 56;

    /* Check active notification items */
    for (int i = 0; i < notif_count && list_y + item_h < (int)scr_h - 40; i++) {
        if (notifs[i].dismissed) continue;
        if (my >= list_y && my < list_y + item_h - 4) {
            dismiss_notif(i);
            shade_open = 0;
            return 1;
        }
        list_y += item_h;
    }

    /* History section interactions */
    list_y += 4;
    if (notif_history_count > 0) {
        /* History header line */
        if (my >= list_y && my < list_y + 28) {
            if (mx >= (int)scr_w - 76 && mx < (int)scr_w - 16) {
                clear_history();
                return 1;
            }
            return 0;
        }
        list_y += 30;

        int max_visible = ((int)scr_h - 40 - list_y) / hist_item_h;
        if (max_visible < 1) max_visible = 1;
        int total = notif_history_count;
        int start = total - max_visible - hist_scroll;
        if (start < 0) start = 0;
        int end = total - hist_scroll;
        if (end > total) end = total;
        if (end - start > max_visible) end = start + max_visible;

        for (int i = start; i < end && list_y + hist_item_h < (int)scr_h - 16; i++) {
            /* Dismiss '×' button at right edge */
            if (my >= list_y && my < list_y + hist_item_h) {
                if (mx >= (int)scr_w - 36 && mx < (int)scr_w - 8) {
                    dismiss_history(i);
                    return 1;
                }
                /* Click on history item body — close shade, could open app later */
                shade_open = 0;
                return 1;
            }
            list_y += hist_item_h;
        }

        /* "N more" scroll up indicator */
        int remaining = total - (end - start);
        if (remaining > 0 && my >= list_y && my < list_y + 20) {
            hist_scroll += max_visible;
            int max_scroll = total - max_visible;
            if (hist_scroll > max_scroll) hist_scroll = max_scroll;
            if (hist_scroll < 0) hist_scroll = 0;
            return 1;
        }
    }

    return 0;
}

/* ── App drawer (macOS Launchpad style) ── */

#define DRAWER_COLS 5
#define DRAWER_PAD  40
#define DRAWER_GAP  20

static int drawer_open;
static char drawer_search[32];
static int drawer_search_pos;
static int drawer_search_active;

typedef struct {
    const char *name;
    const char *label;
    void (*open)(void);
    const char *elf_path;
    uint32_t icon_color;
} drawer_app_t;

static void open_terminal(void) { extern int terminal_open(void); terminal_open(); }
static void open_fmanager(void) { extern int fmanager_open(void); fmanager_open(); }
static void open_settings(void) { extern int settings_open(void); settings_open(); }
static void open_about(void)   { extern int about_open(void); about_open(); }
static void open_calc(void)    { extern void calc_open(void); calc_open(); }
static void open_openweb(void) { extern int openweb_open(void); openweb_open(); }

void zircon_launch_elf(const char *path) {
    extern uint64_t syscall_kernel_rsp;
    uint64_t entry, stack;
    elf_auxv_info_t auxv;
    if (elf_load(path, &entry, &stack, &auxv) < 0) {
        kprintf("zircon: failed to load '%s'\n", path);
        return;
    }
    char *argv[] = { (char *)path, 0 };
    uint64_t rsp = elf_setup_stack(stack, entry, 1, argv, 0, 0, &auxv);
    kprintf("zircon: launching '%s' entry=0x%lx rsp=0x%lx\n", path, entry, rsp);

    /* Create process so syscalls (brk, mmap, exit) work */
    proc_create(path, entry, stack);

    user_mode_set_return(shell_exec_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static void open_zircon_notify(void) { zircon_launch_elf("/bin/zircon-notify"); }
static void open_zircon_clock(void)  { zircon_launch_elf("/bin/zircon-clock"); }
static void open_zircon_info(void)   { zircon_launch_elf("/bin/zircon-info"); }
static void open_android_container(void) { zircon_launch_elf("/bin/android-container"); }
static void open_apk_parser(void)    { zircon_launch_elf("/bin/apk-parser"); }
static void open_code_music(void)    { zircon_launch_elf("/bin/code-music"); }

static drawer_app_t drawer_apps[] = {
    {"Terminal",   "Terminal",       open_terminal,      0, C_GREEN},
    {"FM",         "Files",          open_fmanager,      0, C_BLUE},
    {"Settings",   "Settings",       open_settings,      0, C_SKY},
    {"OpenWeb",    "OpenWeb",        open_openweb,       0, C_MAUVE},
    {"Calc",       "Calculator",     open_calc,          0, C_PEACH},
    {"Notify",     "Notifier",       open_zircon_notify, "/bin/zircon-notify", C_YELLOW},
    {"Clock",      "Z-Clock",        open_zircon_clock,  "/bin/zircon-clock", C_TEAL},
    {"SysInfo",    "System Info",    open_zircon_info,   "/bin/zircon-info", C_SKY},
    {"About",      "About",          open_about,         0, C_MAROON},
    {"Android",    "Android Env",    open_android_container, "/bin/android-container", C_GREEN},
    {"APK",        "APK Parser",     open_apk_parser,    "/bin/apk-parser", C_PEACH},
    {"Music",      "Code Music",     open_code_music,    "/bin/code-music", C_TEAL},
};
#define DRAWER_APP_COUNT ((int)(sizeof(drawer_apps) / sizeof(drawer_apps[0])))

void zircon_draw_drawer(void) {
    if (!drawer_open) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    pm_rect_t clip = {0, 0, sw, sh};

    /* macOS-style frosted glass background */
    pm_composite_rect_alpha(buf, stride, clip, 0, 0, sw, sh, C_BASE, 200);

    /* Search bar — pill shape centered at top */
    int search_h = 32;
    int search_w = sw < 600 ? sw - 40 : 400;
    int search_x = (sw - search_w) / 2;
    int search_y = 20;
    pm_composite_fill_rounded_rect(buf, stride, clip,
        search_x, search_y, search_w, search_h, search_h / 2, 0x55FFFFFF);
    fb_drawstr_px(search_x + 14, search_y + 8, "\xF0\x9F\x94\x8D  Search", 0xAAFFFFFF, 0);
    if (drawer_search_pos > 0) {
        fb_drawstr_px(search_x + 14, search_y + 8, drawer_search, C_TEXT, 0);
    }

    /* Grid layout — larger icons macOS Launchpad style */
    int cols = DRAWER_COLS;
    int cell_w = 96;
    int cell_h = 110;
    int rows = (DRAWER_APP_COUNT + cols - 1) / cols;
    int gw = cols * cell_w + (cols - 1) * DRAWER_GAP;
    int gh = rows * cell_h + (rows - 1) * DRAWER_GAP;
    int gx = (sw - gw) / 2;
    int gy = search_y + search_h + 24;

    /* Center grid vertically if there's enough room */
    int avail_h = sh - gy - 40;
    if (gh < avail_h)
        gy += (avail_h - gh) / 2;

    /* Filtered app list */
    int shown = 0;
    for (int i = 0; i < DRAWER_APP_COUNT; i++) {
        if (drawer_search_pos > 0) {
            const char *label = drawer_apps[i].label;
            int match = 1;
            for (int si = 0; drawer_search[si]; si++) {
                char sc = drawer_search[si];
                if (sc >= 'A' && sc <= 'Z') sc += 32;
                char lc = label[si];
                if (lc >= 'A' && lc <= 'Z') lc += 32;
                if (sc != lc) { match = 0; break; }
            }
            if (!match) continue;
        }

        int col = shown % cols;
        int row = shown / cols;
        int cx = gx + col * (cell_w + DRAWER_GAP);
        int cy = gy + row * (cell_h + DRAWER_GAP);
        int icon_sz = 64;
        int icon_x = cx + (cell_w - icon_sz) / 2;
        int icon_y = cy + 4;

        /* macOS-style icon: rounded rect with subtle inner shadow effect */
        uint32_t ic = drawer_apps[i].icon_color;
        pm_composite_fill_rounded_rect(buf, stride, clip,
            icon_x, icon_y, icon_sz, icon_sz, 14, ic);
        /* Inner highlight (top edge) */
        pm_composite_fill_rounded_rect(buf, stride, clip,
            icon_x + 2, icon_y + 2, icon_sz - 4, icon_sz / 2, 12, 0x30FFFFFF);
        /* Inner shadow (bottom edge) */
        pm_composite_fill_rounded_rect(buf, stride, clip,
            icon_x + 2, icon_y + icon_sz / 2, icon_sz - 4, icon_sz / 2 - 2, 12, 0x20000000);

        /* App label centered below icon */
        const char *label = drawer_apps[i].label;
        int tw = (int)strlen(label) * 8;
        int lx = cx + (cell_w - tw) / 2;
        fb_drawstr_px(lx, cy + 76, label, C_TEXT, 0);
        shown++;
    }

    if (shown == 0 && drawer_search_pos > 0) {
        fb_drawstr_px(sw / 2 - 48, gy + 40, "No results found", C_SUBTEXT0, 0);
    }
}

int zircon_click_drawer(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w;
    (void)scr_h;
    if (!drawer_open) return 0;

    /* Click outside drawer area → close */
    if (my < 10 || my > (int)scr_h - 10) {
        drawer_open = 0;
        drawer_search_active = 0;
        return 0;
    }

    int cols = DRAWER_COLS;
    int cell_w = 96;
    int cell_h = 110;
    int search_h = 32;
    int search_y = 20;
    int rows = (DRAWER_APP_COUNT + cols - 1) / cols;
    int gw = cols * cell_w + (cols - 1) * DRAWER_GAP;
    int gh = rows * cell_h + (rows - 1) * DRAWER_GAP;
    int gx = ((int)scr_w - gw) / 2;
    int gy = search_y + search_h + 24;
    int avail_h = (int)scr_h - gy - 40;
    if (gh < avail_h)
        gy += (avail_h - gh) / 2;

    if (mx < gx || mx >= gx + gw || my < gy || my >= gy + gh) {
        drawer_open = 0;
        drawer_search_active = 0;
        return 0;
    }

    int shown = 0;
    for (int i = 0; i < DRAWER_APP_COUNT; i++) {
        if (drawer_search_pos > 0) {
            const char *label = drawer_apps[i].label;
            int match = 1;
            for (int si = 0; drawer_search[si]; si++) {
                char sc = drawer_search[si];
                if (sc >= 'A' && sc <= 'Z') sc += 32;
                char lc = label[si];
                if (lc >= 'A' && lc <= 'Z') lc += 32;
                if (sc != lc) { match = 0; break; }
            }
            if (!match) { shown++; continue; }
        }

        int col = shown % cols;
        int row_idx = shown / cols;
        int cx = gx + col * (cell_w + DRAWER_GAP);
        int cy = gy + row_idx * (cell_h + DRAWER_GAP);
        if (mx >= cx && mx < cx + cell_w && my >= cy && my < cy + cell_h) {
            drawer_open = 0;
            drawer_search_active = 0;
            if (drawer_apps[i].open)
                drawer_apps[i].open();
            return 1;
        }
        shown++;
    }

    return 0;
}

void zircon_toggle_drawer(void) {
    drawer_open = !drawer_open;
    if (drawer_open) {
        drawer_search_pos = 0;
        drawer_search[0] = 0;
        drawer_search_active = 1;
    } else {
        drawer_search_active = 0;
    }
    zdm_log(ZDM_DRAWER_TOGGLE, drawer_open ? "open" : "close");
}

/* ── Zircon Debug Mode (ZDM) integration ── */

void zdm_toggle(void) {
    zdm_set_enabled(!zdm_is_enabled());
    if (zdm_is_enabled())
        zdm_log(ZDM_INIT, "ZDM activated");
}

int zdm_active(void) {
    return zdm_is_enabled();
}

void zdm_draw(void) {
    if (!zdm_is_enabled()) return;
    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    int sw = (int)fb_getwidth();
    int sh = (int)fb_getheight();
    pm_rect_t clip = {0, 0, sw, sh};

    int bx = 10, by = panels_menubar_h() + 4;
    int bw = 360, bh = sh - by - 10;
    pm_composite_fill_rounded_rect(buf, stride, clip,
        bx, by, bw, bh, 6, 0xBB11111B);

    int y = by + 6;
    char line[72];
    fb_drawstr_px(bx + 8, y, "ZDM — Zircon Debug Mode", C_PEACH, 0); y += 14;
    fb_drawstr_px(bx + 8, y, "F11=dump  F12=toggle", C_SUBTEXT0, 0); y += 18;

    int n = zdm_event_count();
    int start = n > 20 ? n - 20 : 0;
    for (int i = start; i < n; i++) {
        const zdm_event_t *ev = zdm_event_at(i);
        if (!ev) continue;
        uint64_t ms = ev->timestamp;
        sprintf(line, "[%llu.%03u] %s",
                ms / 1000, (unsigned)(ms % 1000), ev->text);
        fb_drawstr_px(bx + 8, y, line, C_TEXT, 0); y += 14;
        if (y > sh - 20) break;
    }
}

/* Override zircon_key to handle ZDM toggles */
int zircon_key(int key) {
    if (key == 0x58) { /* F12 — toggle debug mode */
        zdm_toggle();
        return 1;
    }
    if (key == 0x57) { /* F11 — dump debug log */
        zdm_dump();
        return 1;
    }
    if (!drawer_open) return 0;
    if (key == 0x1B) {
        drawer_open = 0;
        drawer_search_active = 0;
        return 1;
    }
    if (key == '\b' || key == 127) {
        if (drawer_search_pos > 0) {
            drawer_search_pos--;
            drawer_search[drawer_search_pos] = 0;
        }
        return 1;
    }
    if (key >= ' ' && key <= '~' && drawer_search_pos < 31) {
        drawer_search[drawer_search_pos++] = (char)key;
        drawer_search[drawer_search_pos] = 0;
        return 1;
    }
    return 0;
}

int zircon_drawer_open(void) {
    return drawer_open;
}

/* ── Quick settings panel ── */

static int qs_open;

void zircon_toggle_qs(void) {
    qs_open = !qs_open;
    if (qs_open) shade_open = 0;
    zdm_log(ZDM_QS_TOGGLE, qs_open ? "open" : "close");
}

void zircon_draw_qs(void) {
    if (!qs_open) return;
    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    int sw = (int)fb_getwidth();
    pm_rect_t clip = {0, 0, sw, (int)fb_getheight()};
    int pw = 280;
    int ph = 160;
    int px = sw - pw - 10;
    int py = panels_menubar_h() + 4;

    pm_composite_fill_rounded_rect(buf, stride, clip,
        px, py, pw, ph, 10, 0xEE313244);

    fb_drawstr_px(px + 12, py + 10, "Quick Settings", C_TEXT, 0);
    fb_drawstr_px(px + 12, py + 34, "Wi-Fi  ● Connected", C_GREEN, 0);
    fb_drawstr_px(px + 12, py + 56, "Bluetooth  ● On", C_SKY, 0);
    fb_drawstr_px(px + 12, py + 78, "Dark Mode  ● Active", C_MAUVE, 0);
}

int zircon_click_qs(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_h;
    if (!qs_open) return 0;
    int pw = 280;
    int ph = 160;
    int px = (int)scr_w - pw - 10;
    int py = panels_menubar_h() + 4;
    if (mx < px || mx >= px + pw || my < py || my >= py + ph) {
        qs_open = 0;
        return 0;
    }
    return 1;
}

/* ── Init ── */

void zircon_init(void) {
    kprintf("zircon: app manager ready (%d built-in apps)\n", DRAWER_APP_COUNT);
    zdm_log(ZDM_INIT, "Zircon app manager initialized");
    zircon_notify("Zircon", "Welcome to the Better Android");
    zircon_notify("System", "Touch gestures enabled: swipe up = drawer, down = notifications");
}

/* ── Draw all Zircon overlays ── */

void zircon_draw(void) {
    notif_draw();
    zircon_draw_qs();
    zircon_draw_drawer();
    shade_draw();
    zdm_draw();
}

/* ── Click dispatch ── */

int zircon_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    if (shade_click(mx, my, scr_w, scr_h)) return 1;
    if (zircon_click_drawer(mx, my, scr_w, scr_h)) return 1;
    if (zircon_click_qs(mx, my, scr_w, scr_h)) return 1;
    return 0;
}

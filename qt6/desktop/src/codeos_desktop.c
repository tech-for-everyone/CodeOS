/* CodeOS Desktop - Main Implementation
 * Full desktop environment with modern UI */

#include "codeos_desktop.h"
#include "codeos_wm.h"
#include "codeos_theme.h"
#include <stdint.h>
#include <string.h>

/* Desktop state */
static int desktop_initialized = 0;
static int desktop_running = 0;
static uint32_t screen_w = 1920;
static uint32_t screen_h = 1080;

/* Menu bar */
#define MENUBAR_HEIGHT 32
static int menubar_hover_item = -1;
static int menubar_apple_menu_open = 0;

/* Dock */
#define DOCK_HEIGHT 72
#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PADDING 8
#define DOCK_MAX_ICONS 12
static int dock_hover_index = -1;
static int dock_anim_offset[DOCK_MAX_ICONS];

/* Launcher */
static int launcher_open = 0;
static int launcher_anim_progress = 0;
static int launcher_search_len = 0;
static char launcher_search[128] = {0};
static int launcher_selected = 0;

/* Quick settings */
static int quick_settings_open = 0;
static int quick_settings_anim = 0;

/* Notifications */
#define MAX_NOTIFICATIONS 8
#define NOTIF_WIDTH 360
#define NOTIF_HEIGHT 80
#define NOTIF_PADDING 8
typedef struct {
    int active;
    char title[64];
    char text[128];
    uint32_t color;
    uint64_t timestamp;
    int anim_progress;
} notification_t;
static notification_t notifications[MAX_NOTIFICATIONS];
static int notif_count = 0;

/* Context menu */
static int ctxmenu_open = 0;
static int ctxmenu_x, ctxmenu_y;
#define CTXMENU_MAX_ITEMS 8
#define CTXMENU_ITEM_HEIGHT 28
typedef struct {
    char label[64];
    void (*callback)(void);
    int separator;
} ctxmenu_item_t;
static ctxmenu_item_t ctxmenu_items[CTXMENU_MAX_ITEMS];
static int ctxmenu_item_count = 0;
static int ctxmenu_hover = -1;

/* App definitions */
typedef struct {
    const char *name;
    const char *icon;
    void (*launch)(void);
    int is_system;
} app_def_t;

/* Forward declarations for apps */
static void app_terminal(void);
static void app_calculator(void);
static void app_settings(void);
static void app_files(void);
static void app_about(void);
static void app_web_browser(void);
static void app_system_monitor(void);
static void app_text_editor(void);
static void app_image_viewer(void);
static void app_store(void);

static const app_def_t apps[] = {
    {"Terminal",       "terminal",      app_terminal,       1},
    {"Calculator",     "calculator",    app_calculator,     0},
    {"Settings",       "settings",      app_settings,       1},
    {"Files",          "files",         app_files,          0},
    {"About",          "about",         app_about,          1},
    {"Web Browser",    "browser",       app_web_browser,    0},
    {"System Monitor", "sysmon",        app_system_monitor, 1},
    {"Text Editor",    "editor",        app_text_editor,    0},
    {"Image Viewer",   "image",         app_image_viewer,   0},
    {"App Store",      "store",         app_store,          1},
};
#define APP_COUNT (sizeof(apps) / sizeof(apps[0]))

/* Dock icons (subset of apps) */
static const int dock_apps[] = {0, 1, 2, 3, 4, 5, 6};
#define DOCK_APP_COUNT (sizeof(dock_apps) / sizeof(dock_apps[0]))

/* ═══════════════════════════════════════════════════════════════════
   App stubs - will be replaced with real implementations
   ═══════════════════════════════════════════════════════════════════ */

static void app_terminal(void) {
    codeos_desktop_create_window(200, 100, 800, 500, "Terminal");
}

static void app_calculator(void) {
    codeos_desktop_create_window(400, 200, 320, 480, "Calculator");
}

static void app_settings(void) {
    codeos_desktop_create_window(100, 50, 900, 600, "Settings");
}

static void app_files(void) {
    codeos_desktop_create_window(150, 80, 850, 550, "Files");
}

static void app_about(void) {
    codeos_desktop_create_window(300, 150, 480, 360, "About CodeOS");
}

static void app_web_browser(void) {
    codeos_desktop_create_window(50, 40, 1200, 700, "Web Browser");
}

static void app_system_monitor(void) {
    codeos_desktop_create_window(250, 120, 700, 500, "System Monitor");
}

static void app_text_editor(void) {
    codeos_desktop_create_window(180, 90, 900, 650, "Text Editor");
}

static void app_image_viewer(void) {
    codeos_desktop_create_window(350, 180, 640, 480, "Image Viewer");
}

static void app_store(void) {
    codeos_desktop_create_window(100, 50, 1000, 650, "App Store");
}

/* ═══════════════════════════════════════════════════════════════════
   Desktop API Implementation
   ═══════════════════════════════════════════════════════════════════ */

int codeos_desktop_init(void) {
    if (desktop_initialized) return 1;

    codeos_theme_init();
    wm_init();

    screen_w = 1920; /* TODO: get from framebuffer */
    screen_h = 1080;

    memset(notifications, 0, sizeof(notifications));
    memset(dock_anim_offset, 0, sizeof(dock_anim_offset));

    desktop_initialized = 1;
    return 1;
}

void codeos_desktop_run(void) {
    if (!desktop_initialized) return;
    desktop_running = 1;

    /* Main event loop would go here */
    /* In CodeOS this runs in kernel mode */
}

void codeos_desktop_stop(void) {
    desktop_running = 0;
}

int codeos_desktop_active(void) {
    return desktop_initialized && desktop_running;
}

/* Window management */
int codeos_desktop_create_window(int x, int y, int w, int h, const char *title) {
    const codeos_theme_t *theme = codeos_theme_current();
    return wm_create_window(x, y, w, h, title, theme->win_bg);
}

void codeos_desktop_close_window(int id) {
    wm_destroy_window(id);
}

void codeos_desktop_focus_window(int id) {
    wm_focus_window(id);
}

void codeos_desktop_minimize_window(int id) {
    wm_minimize_window(id);
}

void codeos_desktop_maximize_window(int id) {
    wm_maximize_window(id);
}

void codeos_desktop_restore_window(int id) {
    wm_restore_window(id);
}

void codeos_desktop_move_window(int id, int x, int y) {
    wm_move_window(id, x, y);
}

void codeos_desktop_resize_window(int id, int w, int h) {
    wm_resize_window(id, w, h);
}

void codeos_desktop_set_window_title(int id, const char *title) {
    wm_set_window_title(id, title);
}

int codeos_desktop_window_count(void) {
    return wm_window_count();
}

int codeos_desktop_focused_window(void) {
    return wm_focused_window();
}

/* App launching */
void codeos_desktop_launch_app(const char *name) {
    for (int i = 0; i < (int)APP_COUNT; i++) {
        if (strcmp(apps[i].name, name) == 0) {
            if (apps[i].launch) apps[i].launch();
            return;
        }
    }
}

void codeos_desktop_toggle_launcher(void) {
    launcher_open = !launcher_open;
    launcher_anim_progress = launcher_open ? 0 : 100;
    launcher_search[0] = 0;
    launcher_search_len = 0;
    launcher_selected = 0;
}

void codeos_desktop_toggle_quick_settings(void) {
    quick_settings_open = !quick_settings_open;
    quick_settings_anim = quick_settings_open ? 0 : 100;
}

/* Theming */
void codeos_desktop_set_theme(const char *theme_name) {
    codeos_theme_set(theme_name);
}

void codeos_desktop_set_accent_color(uint32_t color) {
    codeos_theme_set_accent(color);
}

void codeos_desktop_set_wallpaper(const char *path) {
    (void)path;
    /* TODO: load and set wallpaper */
}

uint32_t codeos_desktop_screen_width(void) { return screen_w; }
uint32_t codeos_desktop_screen_height(void) { return screen_h; }

/* ═══════════════════════════════════════════════════════════════════
   Menu Bar
   ═══════════════════════════════════════════════════════════════════ */

int codeos_desktop_menubar_height(void) { return MENUBAR_HEIGHT; }

void codeos_desktop_draw_menubar(uint32_t *fb, int stride) {
    const codeos_theme_t *theme = codeos_theme_current();
    (void)fb; (void)stride; (void)theme;
    /* Drawing code would go here - using framebuffer directly */
}

int codeos_desktop_menubar_click(int mx, int my) {
    if (my < 0 || my >= MENUBAR_HEIGHT) return 0;
    /* Handle menubar clicks */
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
   Dock
   ═══════════════════════════════════════════════════════════════════ */

int codeos_desktop_dock_height(void) { return DOCK_HEIGHT; }

void codeos_desktop_draw_dock(uint32_t *fb, int stride) {
    (void)fb; (void)stride;
    /* Dock drawing with animation */
}

int codeos_desktop_dock_click(int mx, int my) {
    int dock_y = (int)screen_h - DOCK_HEIGHT;
    if (my < dock_y || my >= (int)screen_h) return -1;

    int dock_w = DOCK_APP_COUNT * (DOCK_ICON_SIZE + DOCK_ICON_PADDING) + DOCK_ICON_PADDING * 2;
    int dock_x = ((int)screen_w - dock_w) / 2;

    for (int i = 0; i < (int)DOCK_APP_COUNT; i++) {
        int icon_x = dock_x + DOCK_ICON_PADDING + i * (DOCK_ICON_SIZE + DOCK_ICON_PADDING);
        if (mx >= icon_x && mx < icon_x + DOCK_ICON_SIZE) {
            return dock_apps[i];
        }
    }
    return -1;
}

void codeos_desktop_dock_hover(int mx, int my) {
    int dock_y = (int)screen_h - DOCK_HEIGHT;
    dock_hover_index = -1;
    if (my < dock_y || my >= (int)screen_h) return;

    int dock_w = DOCK_APP_COUNT * (DOCK_ICON_SIZE + DOCK_ICON_PADDING) + DOCK_ICON_PADDING * 2;
    int dock_x = ((int)screen_w - dock_w) / 2;

    for (int i = 0; i < (int)DOCK_APP_COUNT; i++) {
        int icon_x = dock_x + DOCK_ICON_PADDING + i * (DOCK_ICON_SIZE + DOCK_ICON_PADDING);
        if (mx >= icon_x && mx < icon_x + DOCK_ICON_SIZE) {
            dock_hover_index = i;
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Launcher (App Grid)
   ═══════════════════════════════════════════════════════════════════ */

void codeos_desktop_draw_launcher(uint32_t *fb, int stride) {
    (void)fb; (void)stride;
    /* Full-screen launcher with search and app grid */
}

int codeos_desktop_launcher_click(int mx, int my) {
    if (!launcher_open) return -1;
    /* Handle launcher clicks */
    return -1;
}

void codeos_desktop_launcher_key(int key) {
    if (!launcher_open) return;
    /* Handle keyboard input in launcher */
    (void)key;
}

/* ═══════════════════════════════════════════════════════════════════
   Quick Settings
   ═══════════════════════════════════════════════════════════════════ */

void codeos_desktop_draw_quick_settings(uint32_t *fb, int stride) {
    (void)fb; (void)stride;
    /* macOS-style control center */
}

int codeos_desktop_quick_settings_click(int mx, int my) {
    if (!quick_settings_open) return 0;
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
   Notification Center
   ═══════════════════════════════════════════════════════════════════ */

void codeos_desktop_push_notification(const char *title, const char *text, uint32_t color) {
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifications[i].active) {
            notifications[i].active = 1;
            strncpy(notifications[i].title, title, sizeof(notifications[i].title) - 1);
            strncpy(notifications[i].text, text, sizeof(notifications[i].text) - 1);
            notifications[i].color = color;
            notifications[i].timestamp = 0; /* TODO: get current time */
            notifications[i].anim_progress = 0;
            notif_count++;
            return;
        }
    }
}

void codeos_desktop_draw_notifications(uint32_t *fb, int stride) {
    (void)fb; (void)stride;
    /* Draw notification stack */
}

int codeos_desktop_notification_click(int mx, int my) {
    /* Handle notification clicks */
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
   Context Menu
   ═══════════════════════════════════════════════════════════════════ */

void codeos_desktop_open_context_menu(int x, int y, const ctxmenu_item_t *items, int count) {
    ctxmenu_x = x;
    ctxmenu_y = y;
    ctxmenu_item_count = count < CTXMENU_MAX_ITEMS ? count : CTXMENU_MAX_ITEMS;
    memcpy(ctxmenu_items, items, ctxmenu_item_count * sizeof(ctxmenu_item_t));
    ctxmenu_open = 1;
    ctxmenu_hover = -1;
}

void codeos_desktop_close_context_menu(void) {
    ctxmenu_open = 0;
}

void codeos_desktop_draw_context_menu(uint32_t *fb, int stride) {
    (void)fb; (void)stride;
    /* Draw context menu */
}

int codeos_desktop_context_menu_click(int mx, int my) {
    if (!ctxmenu_open) return 0;
    /* Handle context menu clicks */
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
   Desktop Context Menu Items
   ═══════════════════════════════════════════════════════════════════ */

static void ctx_new_terminal(void) { app_terminal(); }
static void ctx_new_window(void) { app_files(); }
static void ctx_settings(void) { app_settings(); }
static void ctx_system_monitor(void) { app_system_monitor(); }

/* ═══════════════════════════════════════════════════════════════════
   Event Handling
   ═══════════════════════════════════════════════════════════════════ */

void codeos_desktop_handle_click(int mx, int my, int button) {
    if (button == 1) { /* Left click */
        /* Check context menu first */
        if (ctxmenu_open) {
            codeos_desktop_context_menu_click(mx, my);
            return;
        }

        /* Check launcher */
        if (launcher_open) {
            int app = codeos_desktop_launcher_click(mx, my);
            if (app >= 0) {
                codeos_desktop_toggle_launcher();
                if (apps[app].launch) apps[app].launch();
            }
            return;
        }

        /* Check quick settings */
        if (quick_settings_open) {
            codeos_desktop_quick_settings_click(mx, my);
            return;
        }

        /* Check notifications */
        if (codeos_desktop_notification_click(mx, my)) return;

        /* Check menubar */
        if (my < MENUBAR_HEIGHT) {
            codeos_desktop_menubar_click(mx, my);
            return;
        }

        /* Check dock */
        int dock_app = codeos_desktop_dock_click(mx, my);
        if (dock_app >= 0) {
            if (apps[dock_app].launch) apps[dock_app].launch();
            return;
        }

        /* Check windows */
        int win_id = wm_hit_test(mx, my);
        if (win_id >= 0) {
            wm_focus_window(win_id);
            /* Check window decorations */
            wm_window_t *win = wm_get_window(win_id);
            if (win) {
                int title_h = 32;
                if (my < win->y + title_h) {
                    /* Title bar - start drag */
                    if (mx >= win->x + win->w - 48) {
                        /* Close button */
                        wm_destroy_window(win_id);
                    } else if (mx >= win->x + win->w - 80) {
                        /* Maximize button */
                        wm_maximize_window(win_id);
                    } else if (mx >= win->x + win->w - 112) {
                        /* Minimize button */
                        wm_minimize_window(win_id);
                    } else {
                        wm_begin_drag(win_id, mx, my);
                    }
                }
            }
        }
    } else if (button == 2) { /* Right click */
        /* Desktop context menu */
        int win_id = wm_hit_test(mx, my);
        if (win_id < 0 && my > MENUBAR_HEIGHT && my < (int)screen_h - DOCK_HEIGHT) {
            ctxmenu_item_t items[] = {
                {"New Terminal", ctx_new_terminal, 0},
                {"New Window", ctx_new_window, 0},
                {"", 0, 1},
                {"Settings", ctx_settings, 0},
                {"System Monitor", ctx_system_monitor, 0},
            };
            codeos_desktop_open_context_menu(mx, my, items, 5);
        }
    }
}

void codeos_desktop_handle_drag(int mx, int my) {
    wm_update_drag(mx, my);
    wm_update_resize(mx, my);
}

void codeos_desktop_handle_release(void) {
    wm_end_drag();
    wm_end_resize();
}

void codeos_desktop_handle_key(int key) {
    /* Global shortcuts */
    if (launcher_open) {
        codeos_desktop_launcher_key(key);
        return;
    }

    /* Alt+F4: close focused window */
    if (key == 0x20) { /* TODO: proper key code */
        int focused = wm_focused_window();
        if (focused >= 0) wm_destroy_window(focused);
    }

    /* Super key: toggle launcher */
    if (key == 0x1FF) { /* TODO: proper super key code */
        codeos_desktop_toggle_launcher();
    }
}

void codeos_desktop_update(uint64_t now_ms) {
    wm_update_animations(now_ms);

    /* Update launcher animation */
    if (launcher_open && launcher_anim_progress < 100) {
        launcher_anim_progress += 8;
        if (launcher_anim_progress > 100) launcher_anim_progress = 100;
    } else if (!launcher_open && launcher_anim_progress > 0) {
        launcher_anim_progress -= 8;
        if (launcher_anim_progress < 0) launcher_anim_progress = 0;
    }

    /* Update quick settings animation */
    if (quick_settings_open && quick_settings_anim < 100) {
        quick_settings_anim += 10;
        if (quick_settings_anim > 100) quick_settings_anim = 100;
    } else if (!quick_settings_open && quick_settings_anim > 0) {
        quick_settings_anim -= 10;
        if (quick_settings_anim < 0) quick_settings_anim = 0;
    }

    /* Update notification animations */
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (notifications[i].active && notifications[i].anim_progress < 100) {
            notifications[i].anim_progress += 5;
            if (notifications[i].anim_progress > 100) notifications[i].anim_progress = 100;
        }
    }

    /* Update dock hover animations */
    for (int i = 0; i < DOCK_MAX_ICONS; i++) {
        int target = (i == dock_hover_index) ? 12 : 0;
        if (dock_anim_offset[i] < target) dock_anim_offset[i] += 2;
        if (dock_anim_offset[i] > target) dock_anim_offset[i] -= 2;
    }
}

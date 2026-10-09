/* CodeOS Desktop Environment - Qt6 Based
 * Modern compositor with animations, blur, and theming */

#ifndef CODEOS_DESKTOP_H
#define CODEOS_DESKTOP_H

#include <stdint.h>

/* Desktop dimensions */
#define CODEOS_DESKTOP_VERSION "2.0.0"
#define CODEOS_DESKTOP_NAME "CodeOS Desktop"

/* Color palette - macOS/Windows 11 inspired */
#define COLOR_BG_PRIMARY    0x1E1E2E  /* Dark background */
#define COLOR_BG_SECONDARY  0x313244  /* Surface */
#define COLOR_BG_TERTIARY   0x45475A  /* Surface variant */
#define COLOR_ACCENT        0x89B4FA  /* Blue accent */
#define COLOR_ACCENT_HOVER  0x74C7EC  /* Lighter accent */
#define COLOR_TEXT_PRIMARY   0xCDD6F4  /* Primary text */
#define COLOR_TEXT_SECONDARY 0xA6ADC8  /* Secondary text */
#define COLOR_BORDER        0x585B70  /* Border */
#define COLOR_SHADOW        0x11111B  /* Shadow */
#define COLOR_DOCK_BG       0x1E1E2ECC /* Dock background (with alpha) */
#define COLOR_MENUBAR_BG    0x181825E6 /* Menu bar background */
#define COLOR_PANEL_BG      0x1E1E2EF0 /* Panel background */
#define COLOR_NOTIFICATION  0x45475AEE /* Notification background */

/* Window decoration colors */
#define COLOR_WIN_TITLE     0xCDD6F4
#define COLOR_WIN_BG        0x1E1E2E
#define COLOR_WIN_BORDER    0x585B70
#define COLOR_WIN_CLOSE     0xF38BA8  /* Red close button */
#define COLOR_WIN_MINIMIZE  0xF9E2AF  /* Yellow minimize */
#define COLOR_WIN_MAXIMIZE  0xA6E3A1  /* Green maximize */

/* Animation durations (ms) */
#define ANIM_WINDOW_OPEN    250
#define ANIM_WINDOW_CLOSE   200
#define ANIM_WINDOW_MINIMIZE 300
#define ANIM_DOCK_HOVER     150
#define ANIM_LAUNCHER_OPEN  300
#define ANIM_NOTIFICATION   400
#define ANIM_BLUR_FADE      200

/* Desktop API */
int  codeos_desktop_init(void);
void codeos_desktop_run(void);
void codeos_desktop_stop(void);
int  codeos_desktop_active(void);

/* Window management */
int  codeos_desktop_create_window(int x, int y, int w, int h, const char *title);
void codeos_desktop_close_window(int id);
void codeos_desktop_focus_window(int id);
void codeos_desktop_minimize_window(int id);
void codeos_desktop_maximize_window(int id);
void codeos_desktop_restore_window(int id);
void codeos_desktop_move_window(int id, int x, int y);
void codeos_desktop_resize_window(int id, int w, int h);
void codeos_desktop_set_window_title(int id, const char *title);
int  codeos_desktop_window_count(void);
int  codeos_desktop_focused_window(void);

/* App launching */
void codeos_desktop_launch_app(const char *name);
void codeos_desktop_toggle_launcher(void);
void codeos_desktop_toggle_quick_settings(void);

/* Theming */
void codeos_desktop_set_theme(const char *theme_name);
void codeos_desktop_set_accent_color(uint32_t color);
void codeos_desktop_set_wallpaper(const char *path);

/* Status */
uint32_t codeos_desktop_screen_width(void);
uint32_t codeos_desktop_screen_height(void);

#endif /* CODEOS_DESKTOP_H */

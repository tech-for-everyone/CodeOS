/* android-launcher — CodeOS Android app: home screen / app drawer.
 *
 * The flagship "show-full-ui" screen: status bar, clock, big app icon
 * grid (two rows per screen), a dock with phone essentials, and a search
 * pill. Tapping a grid cell highlights it and prints the launch hint
 * (cross-process launch is `waydroid app launch <app>` in the shell).
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

typedef struct { const char *name; const char *label; uint32_t color; } launcher_app_t;

static const launcher_app_t apps[] = {
    {"android-browser",    "Browser",    AU_BLUE},
    {"android-calendar",   "Calendar",   AU_RED},
    {"android-camera",     "Camera",     AU_GREEN},
    {"android-clock",      "Clock",      AU_TEAL},
    {"android-dialer",     "Phone",      AU_GREEN},
    {"android-keyboard",   "Keyboard",   AU_AMBER},
    {"android-music",      "Music",      AU_PURPLE},
    {"android-settings",   "Settings",   AU_TEXT2},
    {0, 0, 0},
};

static const char *dock[4] = { "Phone", "Browser", "Music", "Settings" };

static void secs_to_hms(long s, int *hh, int *mm) {
    int tod = (int)(s % 86400);
    *hh = tod / 3600;
    *mm = (tod % 3600) / 60;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Launcher", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    int sel = -1;

    while (1) {
        int hh = 0, mm = 0;
        secs_to_hms((long)sys_time(), &hh, &mm);

        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Android", hh, mm);

            /* clock widget */
            char big[40];
            snprintf(big, sizeof(big), "%02d:%02d", hh, mm);
            au_text(a, a->width / 2 - 36, 52, AU_TEXT, big);

            /* search pill */
            au_button(a, 16, 110, a->width - 32, 34, "Search apps and more...",
                      AU_SURFACE2, AU_TEXT2);

            /* app grid: 3 cols x 3 rows */
            int iw = 72, ih = 84, gx = 18, gy = 164;
            int n = 0;
            for (int i = 0; apps[i].name && n < 9; i++, n++) {
                int col = n % 3, row = n / 3;
                int x = gx + col * (iw + 16);
                int y = gy + row * (ih + 14);
                au_rect(a, x + 14, y, iw - 28, iw - 28, apps[i].color);
                au_text(a, x + 38 - ((int)strlen(apps[i].label) * 3), y + iw - 20,
                        n == sel ? AU_PRIMARY : AU_TEXT2, apps[i].label);
            }

            /* dock */
            au_rect(a, 10, a->height - 64, a->width - 20, 54, AU_SURFACE2);
            for (int i = 0; i < 4; i++) {
                int x = 20 + i * ((a->width - 60) / 4);
                au_text(a, x, a->height - 44, AU_TEXT, dock[i]);
            }
            au_flush(a);
        } else if (a->frame == 0) {
            puts("android-launcher: Android home screen");
            puts("  installed apps:");
            for (int i = 0; apps[i].name; i++)
                printf("    %-20s %s\n", apps[i].name, apps[i].label);
            puts("  use 'waydroid app launch <app>' to open an app");
            a->frame++;
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;
        if (ev == WM_EVENT_KEY && key == 'q') break;

        if (ev == WM_EVENT_MOUSE && mb) {
            int iw = 72, ih = 84, gx = 18, gy = 164;
            int n = 0;
            for (int i = 0; apps[i].name && n < 9; i++, n++) {
                int col = n % 3, row = n / 3;
                int x = gx + col * (iw + 16);
                int y = gy + row * (ih + 14);
                if (mx >= x && mx < x + iw && my >= y && my < y + ih) {
                    sel = i;
                    printf("[launcher] %s selected - run: waydroid app launch %s\n",
                           apps[i].label, apps[i].name);
                }
            }
        }

        if (a->wm != AU_WM_OK) {
            a->frame++;
            if (a->frame > 2) break;
            sys_sleep(300);
        } else {
            sys_sleep(16);
        }
    }

    au_end(a);
    return 0;
}
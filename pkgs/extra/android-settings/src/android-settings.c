/* android-settings — CodeOS Android app: About device + quick toggles.
 *
 * GUI: two-tab-ish settings screen (About / Network toggles). Console mode
 * prints the settings and a short toggle demo, then exits.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

typedef struct { const char *name; const char *value; } prop_t;

static const prop_t props[] = {
    {"ro.product.name",          "codeos"},
    {"ro.product.model",         "CodeOS Android Compat"},
    {"ro.product.device",        "codeos_x86_64"},
    {"ro.build.version.sdk",     "29"},
    {"ro.build.version.release", "10"},
    {"ro.product.cpu.abi",       "x86_64"},
    {"ro.product.abilist",       "x86_64,x86,arm64-v8a,armeabi-v7a"},
    {"dalvik.vm.heapsize",       "64m"},
    {"persist.sys.timezone",     "UTC"},
    {0, 0},
};

static int toggle_wifi = 1;
static int toggle_bt   = 0;
static int toggle_dnd  = 0;

static void draw_toggles(au_app_t *a, int y) {
    au_button(a, 16, y,      a->width - 32, 40, toggle_wifi ? "Wi-Fi  ON" : "Wi-Fi  OFF",
              toggle_wifi ? AU_TEAL : AU_SURFACE2, AU_TEXT);
    au_button(a, 16, y + 52, a->width - 32, 40, toggle_bt   ? "Bluetooth  ON" : "Bluetooth  OFF",
              toggle_bt   ? AU_BLUE : AU_SURFACE2, AU_TEXT);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Settings", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    int last_flip = 0;

    while (1) {
        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Settings", -1, -1);
            au_rect(a, 10, 40, a->width - 20, 26, AU_SURFACE2);
            au_text(a, 20, 46, AU_TEXT, "CONNECTED DEVICES");
            draw_toggles(a, 72);
            au_rect(a, 10, 200, a->width - 20, 26, AU_SURFACE2);
            au_text(a, 20, 206, AU_TEXT, "ABOUT PHONE");
            int y = 234;
            for (int i = 0; props[i].name && y < a->height - 14; i++, y += 22) {
                au_rect(a, 10, y, a->width - 20, 20, AU_SURFACE);
                au_text(a, 16, y + 3, AU_TEXT2, props[i].name);
                au_text(a, a->width - 150, y + 3, AU_TEXT, props[i].value);
            }
            au_flush(a);
        } else if (a->frame == 0) {
            puts("android-settings: About phone");
            for (int i = 0; props[i].name; i++)
                printf("  [%s]: [%s]\n", props[i].name, props[i].value);
            printf("  toggles: wifi=%d bt=%d dnd=%d\n", toggle_wifi, toggle_bt, toggle_dnd);
            a->frame = 1;
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY && key == 'q') break;
        if (ev == WM_EVENT_KEY && key == 'w') { toggle_wifi = !toggle_wifi; printf("[settings] wifi=%d\n", toggle_wifi); }
        if (ev == WM_EVENT_KEY && key == 'b') { toggle_bt   = !toggle_bt;   printf("[settings] bt=%d\n", toggle_bt); }

        if (ev == WM_EVENT_MOUSE && mb) {
            if (mx >= 16 && mx < a->width - 16) {
                if (my >= 72 && my < 112) toggle_wifi = !toggle_wifi;
                else if (my >= 124 && my < 164) toggle_bt = !toggle_bt;
            }
        }

        if (a->wm != AU_WM_OK) {
            a->frame++;
            /* toggle demo: flip wifi a couple times, then exit */
            if (toggle_wifi != last_flip) { printf("[settings] wifi=%d\n", toggle_wifi); last_flip = toggle_wifi; }
            if (a->frame == 2) toggle_wifi = 0;
            if (a->frame == 3) toggle_wifi = 1;
            if (a->frame > 4) break;
            sys_sleep(300);
        } else {
            sys_sleep(16);
        }
    }

    au_end(a);
    return 0;
}
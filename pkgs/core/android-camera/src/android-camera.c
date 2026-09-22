/* android-camera — CodeOS Android app: viewfinder + shutter.
 *
 * GUI: a fake viewfinder panel with a corner grid, shutter button that
 * "captures" (flash + timestamp), and a small gallery strip. Console mode
 * simulates a couple of captures and exits.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

static int shots;           /* captures taken */
static char last_shot[64];

static void take_shot(au_app_t *a) {
    long now = (long)sys_time();
    int hh = (int)((now % 86400) / 3600);
    int mm = (int)((now % 3600) / 60);
    int ss = (int)(now % 60);
    shots++;
    snprintf(last_shot, sizeof(last_shot), "IMG_%03d  %02d:%02d:%02d", shots, hh, mm, ss);
    if (a && a->wm == AU_WM_OK) {
        /* flash */
        au_rect(a, 0, 0, a->width, a->height, 0xFFFFFFFF);
        au_flush(a);
        sys_sleep(60);
    }
    printf("[camera] captured %s\n", last_shot);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Camera", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    last_shot[0] = 0;
    int shutter_open = 0;

    while (1) {
        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Camera", -1, -1);

            /* viewfinder */
            au_rect(a, 6, 34, a->width - 12, 300, 0xFF2F3B40);
            /* corner guides + center dot (viewfinder reticle) */
            au_rect(a, 12, 40, 14, 14, 0xFF89E0FF);
            au_rect(a, a->width - 26, 40, 14, 14, 0xFF89E0FF);
            au_rect(a, 12, 314, 14, 14, 0xFF89E0FF);
            au_rect(a, a->width - 26, 314, 14, 14, 0xFF89E0FF);
            int cx = a->width / 2, cy = 34 + 150;
            au_rect(a, cx - 2, cy - 14, 4, 28, 0xFF89E0FF);
            au_rect(a, cx - 14, cy - 2, 28, 4, 0xFF89E0FF);

            /* shutter */
            au_button(a, a->width / 2 - 34, 360, 68, 68, "SHOOT",
                      AU_GREEN, AU_TEXT);

            /* gallery strip */
            au_text(a, 16, 448, AU_TEXT2, "RECENT");
            if (last_shot[0])
                au_text(a, 16, 470, AU_TEXT, last_shot);
            else
                au_text(a, 16, 470, AU_TEXT2, "(no photos yet - tap SHOOT)");
            au_flush(a);
        } else if (a->frame == 0) {
            puts("android-camera: viewfinder ready");
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY) {
            if (key == 'q') break;
            if (key == ' ') take_shot(a);
        }
        if (ev == WM_EVENT_MOUSE && mb &&
            mx >= a->width / 2 - 34 && mx < a->width / 2 + 34 &&
            my >= 360 && my < 428) {
            take_shot(a);
        }

        if (a->wm != AU_WM_OK) {
            if (!shutter_open) { take_shot(a); shutter_open = 1; }
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
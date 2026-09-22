/* android-music — CodeOS Android app: music player "Now Playing".
 *
 * GUI: cover-art block, track title/artist, progress bar, transport
 * buttons (prev/play/next) and a track list. Console mode prints the
 * player state and cycles tracks for a few ticks, then exits.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

static const char *tracks[][2] = {
    {"Smooth Operator",      "Sade"},
    {"Midnight City",        "M83"},
    {"Electric Feel",        "MGMT"},
    {"Blinding Lights",      "The Weeknd"},
    {"Dreams",               "Fleetwood Mac"},
    {0, 0},
};

static int track_idx;
static int playing;
static int progress;   /* 0..1000 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Music", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    track_idx = 0;
    playing = 1;
    progress = 120;

    while (1) {
        static char now_playing[160];
        if (playing) progress = (progress + 6) % 1001;

        const char *title = tracks[track_idx][0];
        const char *artist = tracks[track_idx][1];
        snprintf(now_playing, sizeof(now_playing), "* %s - %s",
                 title, artist);

        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Music", -1, -1);

            /* cover art */
            au_rect(a, 60, 52, a->width - 120, 120, AU_PURPLE);
            au_text(a, 90, 104, AU_ON_PRIMARY, "M");

            /* track info */
            au_rect(a, 10, 190, a->width - 20, 46, AU_SURFACE);
            au_text(a, 24, 196, AU_TEXT, title);
            au_text(a, 24, 214, AU_TEXT2, artist);

            /* progress bar */
            au_rect(a, 16, 250, a->width - 32, 10, AU_SURFACE2);
            int pw = (a->width - 32) * progress / 1000;
            if (pw > 0) au_rect(a, 16, 250, pw, 10, AU_PRIMARY);

            /* transport */
            int bw = (a->width - 20 - 24) / 3;
            int by = 272;
            au_button(a, 10, by, bw, 40, "|<", AU_SURFACE2, AU_TEXT);
            au_button(a, 22 + bw, by, bw, 40, playing ? "II" : ">", AU_PRIMARY, AU_ON_PRIMARY);
            au_button(a, 34 + 2 * bw, by, bw, 40, ">|", AU_SURFACE2, AU_TEXT);

            /* track list */
            au_rect(a, 10, 330, a->width - 20, 26, AU_SURFACE2);
            au_text(a, 20, 336, AU_TEXT, "UP NEXT");
            for (int i = 0; tracks[i][0] && i < 5; i++) {
                int y = 362 + i * 30;
                au_rect(a, 10, y, a->width - 20, 26,
                        i == track_idx ? AU_SURFACE2 : AU_SURFACE);
                snprintf(now_playing, sizeof(now_playing), "%d. %s", i + 1, tracks[i][0]);
                au_text(a, 20, y + 5, i == track_idx ? AU_PRIMARY : AU_TEXT,
                        now_playing);
            }
            au_flush(a);
        } else if (a->frame == 0) {
            puts("android-music: Now Playing");
            printf("  %s\n", now_playing);
            printf("  keys: space=play/pause, n=next, p=prev, q=quit\n");
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY) {
            if (key == 'q') break;
            if (key == ' ') playing = !playing;
            if (key == 'n') { track_idx = (track_idx + 1) % 5; progress = 0; }
            if (key == 'p') { track_idx = (track_idx + 4) % 5; progress = 0; }
        }

        if (ev == WM_EVENT_MOUSE && mb) {
            int bw = (a->width - 20 - 24) / 3;
            int by = 272;
            if (my >= by && my < by + 40) {
                if (mx < 22 + bw) { track_idx = (track_idx + 4) % 5; progress = 0; }
                else if (mx < 34 + 2 * bw) playing = !playing;
                else { track_idx = (track_idx + 1) % 5; progress = 0; }
            }
        }

        if (a->wm != AU_WM_OK) {
            if (a->frame == 1) playing = 0;
            if (a->frame == 2) { playing = 1; track_idx = 1; }
            printf("[music] %s (%d%%)\n", now_playing, progress / 10);
            a->frame++;
            if (a->frame > 3) break;
            sys_sleep(400);
        } else {
            sys_sleep(16);
        }
    }

    au_end(a);
    return 0;
}
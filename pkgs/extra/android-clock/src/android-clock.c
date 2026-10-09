/* android-clock — CodeOS Android app: live clock.
 *
 * Run: android-clock [--console-only]
 * GUI: opens a phone-size window via the WM bridge and draws a live clock,
 *      date and an alarm toggle. Closes when the window is closed.
 * Console fallback: prints the clock every second for a few ticks, then exits.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

/* Epoch → UTC-ish breakdown without libc. sys_time() returns epoch seconds. */
static void secs_to_hms(long s, int *hh, int *mm, int *ss, int *day, int *mon, int *yr) {
    long days = s / 86400;
    int tod = (int)(s % 86400);
    *hh = tod / 3600;
    *mm = (tod % 3600) / 60;
    *ss = tod % 60;
    /* Civil from days since epoch (Howard Hinnant's algorithm). */
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long doe = (unsigned long)(z - era * 146097);
    unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned long mp = (5 * doy + 2) / 153;
    unsigned long d = doy - (153 * mp + 2) / 5 + 1;
    unsigned long m = mp < 10 ? mp + 3 : mp - 9;
    *yr = (int)(y + (m <= 2));
    *mon = (int)m;
    *day = (int)d;
}

static const char *month_name(int m) {
    static const char *names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    if (m < 1 || m > 12) return "?";
    return names[m - 1];
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Clock", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) {
        au_end(a);
        return 0;
    }

    int toggled = 0;   /* alarm toggle */
    int last_sec = -1;

    while (1) {
        int hh, mm, ss, d, m, y;
        long now = (long)sys_time();
        secs_to_hms(now, &hh, &mm, &ss, &d, &m, &y);

        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Clock", hh, mm);
            au_rect(a, 10, 100, a->width - 20, 150, AU_SURFACE);
            char big[32];
            snprintf(big, sizeof(big), "%02d:%02d:%02d", hh, mm, ss);
            au_text(a, 40, 128, AU_TEXT, big);

            char date[64];
            snprintf(date, sizeof(date), "%s %d, %d", month_name(m), d, y);
            au_text(a, 60, 190, AU_TEXT2, date);

            au_button(a, 40, 260, a->width - 80, 44,
                      toggled ? "Alarm: ON  (tap to turn off)" : "Alarm: OFF  (tap to set)",
                      toggled ? AU_GREEN : AU_SURFACE2, AU_TEXT);
            au_flush(a);
        } else {
            if (ss != last_sec) {
                printf("[clock] %s %d, %d - %02d:%02d:%02d%s\n",
                       month_name(m), d, y, hh, mm, ss,
                       toggled ? "  [ALARM SET]" : "");
                last_sec = ss;
            }
        }

        /* event pump */
        if (a->wm == AU_WM_OK) {
            int key = 0, mx = 0, my = 0, mb = 0;
            while (1) {
                int ev = au_poll(a, &key, &mx, &my, &mb);
                if (ev == 0) break;
                if (ev == WM_EVENT_CLOSED) goto done;
                if ((ev == WM_EVENT_KEY && key == 'a') ||
                    (ev == WM_EVENT_MOUSE && mb && mx >= 40 && mx < a->width - 40 &&
                     my >= 260 && my < 304)) {
                    toggled = !toggled;
                }
            }
            sys_sleep(16);
        } else {
            /* console demo: run a few ticks then exit deterministically */
            if (ss == last_sec) sys_sleep(250);
            a->frame++;
            if (a->frame > 4) break;
        }
    }

done:
    au_end(a);
    return 0;
}
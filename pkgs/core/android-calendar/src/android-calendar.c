/* android-calendar — CodeOS Android app: month grid.
 *
 * Derives today's date from sys_time() (epoch) and draws the month as a
 * weekday-labeled grid. Console mode prints the month grid as text.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

static void epoch_to_civil(long days, int *day, int *mon, int *yr) {
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

static int days_in_month(int m, int y) {
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 29;
    if (m < 1 || m > 12) return 30;
    return dm[m - 1];
}

static const char *month_name(int m) {
    static const char *names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    return (m >= 1 && m <= 12) ? names[m - 1] : "?";
}

/* weekday of the 1st of month: 0=Sun */
static int first_weekday(int m, int y) {
    /* days since epoch of (y, m, 1)  (civil calendrical arithmetic) */
    long yy = y - (m <= 2 ? 1 : 0);
    long era = yy / 400;
    long yoe = yy - era * 400;
    long mp = m + (m > 2 ? -3 : 9);
    long doy = (153 * mp + 2) / 5 + 1 - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + doe - 719468;
    /* 1970-01-01 (days=0) was a Thursday (4 with Sun=0) */
    return (int)(((days + 4) % 7 + 7) % 7);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Calendar", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    int sel = -1;

    while (1) {
        long now = (long)sys_time();
        long today_days = now / 86400;
        int day, mon, yr;
        epoch_to_civil(today_days, &day, &mon, &yr);

        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Calendar", -1, -1);

            char title[64];
            snprintf(title, sizeof(title), "%s %d", month_name(mon), yr);
            au_text(a, 16, 44, AU_TEXT, title);

            static const char *wd[7] = {"S", "M", "T", "W", "T", "F", "S"};
            int cw = (a->width - 20 - 6 * 6) / 7;
            int y0 = 76;

            for (int i = 0; i < 7; i++) {
                int x = 10 + i * (cw + 6);
                au_text(a, x + cw / 2 - 4, y0, AU_TEXT2, wd[i]);
            }

            int fw = first_weekday(mon, yr);
            int dim = days_in_month(mon, yr);
            int cell = 0;
            for (int d = 1; d <= dim; d++, cell++) {
                int pos = fw + cell;      /* 0-based day-of-week slot */
                int row = pos / 7, col = pos % 7;
                int x = 10 + col * (cw + 6);
                int y = y0 + 20 + row * 34;
                int today = (d == day);
                int is_sel = (d == sel);
                au_rect(a, x, y, cw, 30, today ? AU_PRIMARY : (is_sel ? AU_AMBER : AU_SURFACE));
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", d);
                au_text(a, x + cw / 2 - 4, y + 8,
                        (today || is_sel) ? AU_ON_PRIMARY : AU_TEXT, buf);
            }
            au_flush(a);
        } else if (a->frame == 0) {
            int fw = first_weekday(mon, yr);
            int dim = days_in_month(mon, yr);
            printf("android-calendar: %s %d (today: %d)\n", month_name(mon), yr, day);
            int pos = 0;
            for (int d = 1; d <= dim + fw; d++) {
                char buf[8];
                int dnum = d - fw;
                if (dnum < 1) snprintf(buf, sizeof(buf), "   ");
                else snprintf(buf, sizeof(buf), " %2d%s",
                              dnum, dnum == day ? "*" : "");
                printf("%s", buf);
                pos++;
                if (pos % 7 == 0) puts("");
            }
            if (pos % 7) puts("");
            a->frame++;
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;
        if (ev == WM_EVENT_KEY && key == 'q') break;

        if (ev == WM_EVENT_MOUSE && mb) {
            int cw = (a->width - 20 - 36) / 7;
            int y0 = 76;
            int col = (mx - 10) / (cw + 6);
            int row = (my - (y0 + 20)) / 34;
            int dnum = row * 7 + col - first_weekday(mon, yr) + 1;
            if (dnum >= 1 && dnum <= days_in_month(mon, yr)) {
                sel = dnum;
                printf("[calendar] selected %s %d\n", month_name(mon), dnum);
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
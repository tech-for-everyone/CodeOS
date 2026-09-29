/* android-dialer — CodeOS Android app: dialer keypad.
 *
 * GUI: number display + 3x4 keypad + call/end buttons. Console mode: REPL
 * where digits build a number and Enter "dials" it.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

#define MAX_DIGITS 24

static char number[MAX_DIGITS + 1];
static int  num_len;
static int  in_call;

static void number_reset(void) { number[0] = 0; num_len = 0; }

static void number_push(char c) {
    if (num_len < MAX_DIGITS) { number[num_len++] = c; number[num_len] = 0; }
}

static void number_back(void) {
    if (num_len > 0) { num_len--; number[num_len] = 0; }
}

static const char *pad[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"*", "0", "#"},
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Dialer", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    number_reset();
    int scan = 0;

    while (1) {
        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Phone", -1, -1);

            /* number display */
            au_rect(a, 10, 44, a->width - 20, 56, AU_SURFACE2);
            char disp[64];
            if (in_call)
                snprintf(disp, sizeof(disp), "Calling %s...", number[0] ? number : "(unknown)");
            else if (num_len == 0)
                snprintf(disp, sizeof(disp), "Enter number");
            else
                snprintf(disp, sizeof(disp), "%s", number);
            au_text(a, 24, 62, AU_TEXT, disp);

            /* keypad */
            int kw = (a->width - 20 - 2 * 12) / 3;
            int kh = 52;
            int y0 = 116;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 3; c++) {
                    int x = 10 + c * (kw + 12);
                    int y = y0 + r * (kh + 12);
                    au_button(a, x, y, kw, kh, pad[r][c], AU_SURFACE, AU_TEXT);
                }
            }
            /* call row */
            au_button(a, 10, y0 + 4 * (kh + 12), (a->width - 34) / 2, 44,
                      in_call ? "End" : "Call", in_call ? AU_RED : AU_GREEN,
                      in_call ? AU_ON_PRIMARY : AU_TEXT);
            au_button(a, 10 + (a->width - 30) / 2 + 4, y0 + 4 * (kh + 12),
                      (a->width - 30) / 2 - 8, 44, "Del", AU_SURFACE2, AU_TEXT);
            au_flush(a);
        } else if (a->frame == 0) {
            puts("android-dialer: type digits, Enter dials, 'q' quits");
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY) {
            if (key == 'q') break;
            if (strchr("0123456789*#", (char)key)) {
                if (in_call) in_call = 0;
                number_push((char)key);
            } else if (key == '\n' || key == '\r') {
                printf("[dialer] dialing %s\n", number[0] ? number : "(empty)");
                if (number[0]) in_call = 1;
            } else if (key == '\b' || key == 127) {
                number_back();
            }
        }

        if (ev == WM_EVENT_MOUSE && mb) {
            int kw = (a->width - 20 - 24) / 3;
            int kh = 52;
            int y0 = 116;
            int c = (mx - 10) / (kw + 12);
            int r = (my - y0) / (kh + 12);
            if (r >= 0 && r < 4 && c >= 0 && c < 3) {
                number_push(pad[r][c][0]);
            } else if (my >= y0 + 4 * (kh + 12) && my < y0 + 4 * (kh + 12) + 44) {
                if (mx < (a->width - 30) / 2 + 12) in_call = !in_call;
                else number_back();
            }
        }

        if (a->wm != AU_WM_OK) {
            char buf[64];
            int n = 0, got = 0;
            while (n < 62) {
                char ch;
                int r = sys_read(0, &ch, 1);
                if (r <= 0) break;
                if (ch == '\n' || ch == '\r') { got = 1; break; }
                buf[n++] = ch;
            }
            if (got) {
                buf[n] = 0;
                if (strcmp(buf, "q") == 0) break;
                for (int i = 0; buf[i] && i < MAX_DIGITS; i++)
                    if (strchr("0123456789*#", buf[i])) number_push(buf[i]);
                printf("[dialer] dialing %s%s\n", number, in_call ? " (in call)" : "");
                in_call = 1;
            }
            if (in_call) { scan++; if (scan > 3) { printf("[dialer] call ended\n"); in_call = 0; number_reset(); scan = 0; } }
            if (n <= 0 && !got) { sys_sleep(300); a->frame++; if (a->frame > 6) break; }
        } else {
            sys_sleep(16);
        }
    }

    au_end(a);
    return 0;
}
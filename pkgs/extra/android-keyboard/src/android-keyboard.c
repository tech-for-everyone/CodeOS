/* android-keyboard — CodeOS Android app: on-screen keyboard demo.
 *
 * GUI: a text field on top and a full QWERTY keyboard below; key taps (or
 * typeahead keystrokes) echo into the field. Console mode: type plain text.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

#define TEXT_MAX 96

static char text[TEXT_MAX + 1];
static int  text_len;

static const char *key_layout[3] = {
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm.,",
};

static void text_push(char c) {
    if (text_len < TEXT_MAX) { text[text_len++] = c; text[text_len] = 0; }
}

static void text_back(void) {
    if (text_len > 0) { text_len--; text[text_len] = 0; }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    au_app_t *a = au_start("Keyboard", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    while (1) {
        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Keyboard", -1, -1);

            /* text field */
            au_rect(a, 10, 44, a->width - 20, 44, AU_SURFACE2);
            char disp[TEXT_MAX + 16];
            if (text_len == 0)
                snprintf(disp, sizeof(disp), "Type...");
            else
                snprintf(disp, sizeof(disp), "%s|", text);
            au_text(a, 24, 58, AU_TEXT, disp);

            /* keyboard rows */
            int row_y = 120;
            for (int r = 0; r < 3; r++, row_y += 54) {
                int key_w = 22, gap = 4;
                const char *row = key_layout[r];
                int row_w = (int)strlen(row) * (key_w + gap);
                int x0 = (a->width - row_w) / 2;
                for (int i = 0; row[i]; i++) {
                    int x = x0 + i * (key_w + gap);
                    char k[2] = { row[i], 0 };
                    au_button(a, x, row_y, key_w, 46, k, AU_SURFACE, AU_TEXT);
                }
            }
            /* space + backspace row */
            au_button(a, 40, row_y, a->width - 140, 40, "space", AU_SURFACE, AU_TEXT);
            au_button(a, a->width - 90, row_y, 60, 40, "Del", AU_SURFACE2, AU_TEXT);
            au_flush(a);
        } else if (a->frame == 0) {
            puts("android-keyboard: type text, Backspace deletes, Enter shows it, 'q' quits");
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY) {
            if (key == 'q') break;
            if (key == '\n' || key == '\r') {
                printf("[keyboard] text: \"%s\"\n", text);
                continue;
            }
            if (key == '\b' || key == 127) { text_back(); continue; }
            char c = (char)key;
            if (c >= 32 && c < 127) text_push(c);
        }

        if (ev == WM_EVENT_MOUSE && mb) {
            int row_y = 120;
            for (int r = 0; r < 3; r++) {
                int key_w = 22, gap = 4;
                const char *row = key_layout[r];
                int row_w = (int)strlen(row) * (key_w + gap);
                int x0 = (a->width - row_w) / 2;
                if (my >= row_y + r * 54 && my < row_y + r * 54 + 46) {
                    int i = (mx - x0) / (key_w + gap);
                    if (i >= 0 && i < (int)strlen(row)) text_push(row[i]);
                }
            }
            if (my >= row_y + 3 * 54 && my < row_y + 3 * 54 + 40) {
                if (mx >= a->width - 90) text_back();
                else text_push(' ');
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
                for (int i = 0; buf[i]; i++) text_push(buf[i]);
                printf("[keyboard] text: \"%s\"\n", text);
                a->frame++;
                if (a->frame > 1) break;
            }
            if (n <= 0 && !got) { sys_sleep(200); a->frame++; if (a->frame > 5) break; }
        } else {
            sys_sleep(16);
        }
    }

    au_end(a);
    return 0;
}
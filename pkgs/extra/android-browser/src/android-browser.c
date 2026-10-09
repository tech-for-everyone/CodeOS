/* android-browser — CodeOS Android app: minimal web view.
 *
 * Uses SYSCALL_WEB (OpenWeb backend): a URL/search box on top and the
 * fetched page text in a scroll region. Console mode prints the page text.
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

#define URL_MAX 96
#define PAGE_MAX 512

static char url[URL_MAX];
static int  url_len;
static char page[PAGE_MAX];
static char status[128];

static void url_reset(void) { url[0] = 0; url_len = 0; }

static void url_push(char c) {
    if (url_len < URL_MAX - 1) { url[url_len++] = c; url[url_len] = 0; }
}

static void url_back(void) {
    if (url_len > 0) { url_len--; url[url_len] = 0; }
}

static void do_navigate(void) {
    snprintf(status, sizeof(status), "Loading...");
    int r = url[0] ? sys_web_search(url) : -1;
    if (r < 0) {
        snprintf(status, sizeof(status), "OpenWeb unavailable");
        return;
    }
    for (int i = 0; i < 20; i++) {
        sys_sleep(50);
        int prog = sys_web_tab_progress();
        if (prog >= 100) break;
    }
    int n = sys_web_get_content(page, PAGE_MAX - 1);
    if (n < 0) n = 0;
    page[n] = 0;
    snprintf(status, sizeof(status), "%d bytes fetched%s",
             n, n == 0 ? " (empty page)" : "");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    url_reset();
    page[0] = 0;
    snprintf(status, sizeof(status), "Search the web or type a query");

    au_app_t *a = au_start("Browser", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    int shown = 0;

    while (1) {
        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Browser", -1, -1);

            /* address bar */
            au_rect(a, 10, 40, a->width - 20, 34, AU_SURFACE2);
            char ab[URL_MAX + 16];
            snprintf(ab, sizeof(ab), ">  %s", url_len ? url : "Type to search...");
            au_text(a, 16, 48, AU_TEXT, ab);

            au_text(a, 14, 82, AU_TEXT2, status);

            /* page text */
            int y = 102;
            char *save = 0;
            char buf[PAGE_MAX];
            snprintf(buf, sizeof(buf), "%s", page);
            for (int i = 0; buf[i] && y < a->height - 10; ) {
                int end = i;
                while (buf[end] && buf[end] != '\n' && end - i < 40) end++;
                char line[48];
                int n = end - i;
                if (n > 40) n = 40;
                memcpy(line, buf + i, n);
                line[n] = 0;
                au_rect(a, 10, y, a->width - 20, 18, AU_SURFACE);
                au_text(a, 16, y + 2, AU_TEXT2, line);
                y += 20;
                i += n;
                while (buf[i] == '\n') i++;
                (void)save;
            }
            if (!page[0]) {
                au_rect(a, 10, 102, a->width - 20, 60, AU_SURFACE);
                au_text(a, 20, 112, AU_TEXT2, "No page loaded yet.");
                au_text(a, 20, 130, AU_TEXT2, "Tap the address bar, type, press Enter.");
            }
            au_flush(a);
        } else if (!shown) {
            puts("android-browser: type a query and press Enter; 'q' quits");
            shown = 1;
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY) {
            if (key == 'q') break;
            if (key == '\n' || key == '\r') {
                printf("[browser] search: %s\n", url_len ? url : "(empty)");
                do_navigate();
                printf("[browser] %s\n", status);
                if (a->wm == AU_WM_OK) {
                    /* keep query; result in page */
                }
                continue;
            }
            if (key == '\b' || key == 127) { url_back(); continue; }
            char c = (char)key;
            if (c >= 32 && c < 127 && c != '`') url_push(c);
        }

        if (a->wm != AU_WM_OK) {
            char buf[128];
            int n = 0, got = 0;
            while (n < 126) {
                char ch;
                int r = sys_read(0, &ch, 1);
                if (r <= 0) break;
                if (ch == '\n' || ch == '\r') { got = 1; break; }
                buf[n++] = ch;
            }
            if (got) {
                buf[n] = 0;
                if (strcmp(buf, "q") == 0) break;
                url_reset();
                for (int i = 0; buf[i]; i++) url_push(buf[i]);
                do_navigate();
                printf("[browser] %s\n", status);
                printf("[browser] page:\n%s\n", page);
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
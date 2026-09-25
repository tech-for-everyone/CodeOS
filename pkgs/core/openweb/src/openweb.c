#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "openweb.h"
#include "ow_html.h"
#include <unistd.h>

#define MAX_URL 512
#define MAX_LINES 200
#define SCROLL_STEP 10

static char g_url[MAX_URL] = "";
static int  g_scroll = 0;
static int  g_dirty = 1;
static int  g_show_help = 0;
static char g_status_msg[256] = "";

static void clear_screen(void) {
    printf("\x1b[2J");
}

static void goto_home(void) {
    printf("\x1b[H");
}

static void render_help(void) {
    clear_screen();
    goto_home();

    printf("\x1b[1mOpenWeb Help\x1b[0m\n");
    printf("\x1b[33mNavigation:\x1b[0m\n");
    printf("  u/d               Scroll up/down\n");
    printf("  p/P               Jump to top/bottom\n");
    printf("  Tab               Switch tabs\n");
    printf("\x1b[33mURL Input:\x1b[0m\n");
    printf("  Type to enter URL\n");
    printf("  \x1b[1mEnter\x1b[0m    Load URL or select tab\n");
    printf("  \x1b[1mBackspace\x1b[0m  Delete character\n");
    printf("\x1b[33mActions:\x1b[0m\n");
    printf("  \x1b[1mt\x1b[0m       New tab\n");
    printf("  \x1b[1mc/x\x1b[0m     Close current tab\n");
    printf("  \x1b[1mb/f\x1b[0m     Back / Forward (history)\n");
    printf("  \x1b[1ms\x1b[0m       Stop loading\n");
    printf("  \x1b[1mr\x1b[0m       Reload page or active tab\n");
    printf("  \x1b[1mq\x1b[0m      Quit\n");
    printf("  \x1b[1mEsc\x1b[0m      Exit app\n");
    printf("  \x1b[1m?\x1b[0m       Show/hide help\n");
    printf("\x1b[33mStatus:\x1b[0m\n");
    if (g_status_msg[0]) {
        printf("  %s\n", g_status_msg);
    }
    printf("\x1b[33mTips:\x1b[0m\n");
    printf("  \x1b[1mNumber keys\x1b[0m  Switch to tab by number\n");
    printf("  \x1b[1mH/L\x1b[0m       Toggle help\n");
    g_dirty = 0;
}

static void render_page(void) {
    int active = sys_web_tab_active_get();
    if (active < 0) active = 0;
    if (sys_web_tab_set_active(active) < 0) return;

    web_state_t st;
    memset(&st, 0, sizeof(st));
    if (sys_web_get_info(&st) < 0) return;

    char content[OW_CONTENT_MAX];
    int clen = st.content_len;
    if (clen > 0 && clen <= OW_CONTENT_MAX) {
        int n = sys_web_get_content(content, clen);
        if (n > 0) {
            content[n] = 0;
            render_html(content, n);
        }
    }

    clear_screen();
    goto_home();

    printf("\x1b[1mOpenWeb v2.0\x1b[0m");
    printf(" \x1b[90m%c%d%c\x1b[0m", active < 0 ? '[' : '(', active + 1, active < 0 ? ']' : ')');
    printf(" \x1b[90m%d tabs\x1b[0m", sys_web_tab_count());
    if (g_show_help) {
        printf(" \x1b[33m[HELP]\x1b[0m");
    }
    if (st.can_go_back || st.can_go_forward) {
        printf(" \x1b[36m%s%s\x1b[0m",
               st.can_go_back ? "\xe2\x80\xb9" : "-",
               st.can_go_forward ? "\xe2\x80\xba" : "-");
    }
    if (st.loading) {
        int prog = sys_web_tab_progress();
        printf(" \x1b[33mLOADING %d%%\x1b[0m", prog);
    }
    if (st.url[0]) {
        printf(" \x1b[36m%s\x1b[0m", st.url);
    }
    if (st.loading) {
        printf(" ...");
    }
    printf("\n");

    printf("\x1b[90m--- %s ---\x1b[0m\n", st.status);

    if (ow_txt_lines > 0) {
        int start = g_scroll;
        int end = ow_txt_lines;
        if (end - start > MAX_LINES) end = start + MAX_LINES;

        printf("\x1b[1mContent:\x1b[0m\n");
        for (int i = start; i < end; i++) {
            if (i >= ow_txt_lines) break;
            int link_idx = -1;
            for (int li = 0; li < ow_link_cnt; li++) {
                if (ow_links[li].line == i) { link_idx = li; break; }
            }
            if (link_idx >= 0) {
                printf("  \x1b[34m[%d]\x1b[0m \x1b[1m%s\x1b[0m\n", link_idx + 1, ow_txt[i]);
            } else {
                printf("  %s\n", ow_txt[i]);
            }
        }

        if (ow_txt_lines > MAX_LINES) {
            printf("\x1b[90m... (%d/%d lines, scroll position: %d%%)\x1b[0m\n",
                   g_scroll, ow_txt_lines, (g_scroll * 100) / (ow_txt_lines - MAX_LINES + 1));
        }
    }

    printf("\x1b[1mURL:\x1b[0m ");
    if (g_url[0]) {
        printf("\x1b[32m%s\x1b[0m", g_url);
    }
    printf("\n");

    if (g_status_msg[0]) {
        printf("\x1b[90mStatus: %s\x1b[0m\n", g_status_msg);
    }

    if (g_show_help) {
        render_help();
        return;
    }

    printf("\x1b[2m--- Help: ? Show/Hide  PageUp/PgDown Jump  Esc Quit  u/d Scroll  Tab Switch  Enter Navigate ---\x1b[0m");
    g_dirty = 0;
}

static void handle_key(int key) {
    if (key == '?') {
        g_show_help = !g_show_help;
        g_dirty = 1;
        return;
    }
    if (key == '\x1b') {
        sys_exit(0);
    }
    if (key == '\n' || key == '\r') {
        if (strlen(g_url) == 0) return;
        if (g_url[0] >= '0' && g_url[0] <= '9') {
            int idx = atoi(g_url) - 1;
            g_url[0] = 0;
            if (idx >= 0 && idx < sys_web_tab_count()) sys_web_tab_set_active(idx);
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "Navigating to: %s", g_url);
            sys_web_navigate(g_url);
            g_url[0] = 0;
            g_scroll = 0;
            g_dirty = 1;
        }
        return;
    }
    if (key == '\b' || key == 127) {
        int len = strlen(g_url);
        if (len > 0) {
            g_url[len - 1] = 0;
            snprintf(g_status_msg, sizeof(g_status_msg), "URL edited");
            g_dirty = 1;
        }
        return;
    }
    if (key == 't' || key == 'T') {
        int cnt = sys_web_tab_count();
        if (cnt >= 12) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Max tabs reached");
            g_dirty = 1;
            return;
        }
        sys_web_tab_new("about:blank");
        g_url[0] = 0;
        g_scroll = 0;
        snprintf(g_status_msg, sizeof(g_status_msg), "New tab (%d)", sys_web_tab_count());
        g_dirty = 1;
        return;
    }
    if (key == 'c' || key == 'C' || key == 'x' || key == 'X') {
        int cnt = sys_web_tab_count();
        int active = sys_web_tab_active_get();
        if (cnt > 1) {
            sys_web_tab_close(active);
            snprintf(g_status_msg, sizeof(g_status_msg), "Closed tab (%d left)", sys_web_tab_count());
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "Cannot close last tab");
        }
        g_url[0] = 0;
        g_scroll = 0;
        g_dirty = 1;
        return;
    }
    if (key == 'q' || key == 'Q') {
        sys_exit(0);
    }
    if (key == 'r' || key == 'R') {
        if (g_url[0]) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Reloading: %s", g_url);
            sys_web_navigate(g_url);
        } else {
            web_state_t st;
            memset(&st, 0, sizeof(st));
            sys_web_get_info(&st);
            if (st.url[0]) {
                snprintf(g_status_msg, sizeof(g_status_msg), "Reloading: %s", st.url);
                sys_web_navigate(st.url);
            } else {
                snprintf(g_status_msg, sizeof(g_status_msg), "No URL to reload");
            }
        }
        g_dirty = 1;
        return;
    }
    if (key == 'u' || key == 'U' || key == 'p' || key == 'P') {
        if (key == 'p' || key == 'P') {
            g_scroll = 0;
        } else {
            g_scroll = g_scroll > 0 ? g_scroll - SCROLL_STEP : 0;
        }
        snprintf(g_status_msg, sizeof(g_status_msg), "Scroll position: %d", g_scroll);
        g_dirty = 1;
        return;
    }
    if (key == 'd' || key == 'D' || key == 'n' || key == 'N') {
        if (key == 'n' || key == 'N') {
            g_scroll = ow_txt_lines - MAX_LINES;
            if (g_scroll < 0) g_scroll = 0;
        } else {
            g_scroll += SCROLL_STEP;
        }
        snprintf(g_status_msg, sizeof(g_status_msg), "Scroll position: %d", g_scroll);
        g_dirty = 1;
        return;
    }
    if (key == '\t') {
        int a = sys_web_tab_active_get();
        int c = sys_web_tab_count();
        if (c > 0) sys_web_tab_set_active((a + 1) % c);
        snprintf(g_status_msg, sizeof(g_status_msg), "Switched to tab %d", (a + 1) % c);
        g_dirty = 1;
        return;
    }    if (key == 'h' || key == 'H' || key == 'l' || key == 'L') {
        g_show_help = (key == 'h' || key == 'H');
        snprintf(g_status_msg, sizeof(g_status_msg), "%s help", g_show_help ? "Showing" : "Hiding");
        g_dirty = 1;
        return;
    }
    if (key == 'b' || key == 'B') {
        sys_web_go_back();
        snprintf(g_status_msg, sizeof(g_status_msg), "Back (history)");
        g_dirty = 1;
        return;
    }
    if (key == 'f' || key == 'F') {
        sys_web_go_forward();
        snprintf(g_status_msg, sizeof(g_status_msg), "Forward (history)");
        g_dirty = 1;
        return;
    }
    if (key == 's' || key == 'S') {
        sys_web_stop();
        snprintf(g_status_msg, sizeof(g_status_msg), "Stop loading");
        g_dirty = 1;
        return;
    }
    if (key >= ' ' && key <= '~') {
        int len = strlen(g_url);
        if (len < MAX_URL - 1) {
            g_url[len] = (char)key;
            g_url[len + 1] = 0;
            snprintf(g_status_msg, sizeof(g_status_msg), "URL: %s", g_url);
            g_dirty = 1;
        }
    }
}

int main(void) {
    printf("\x1b[?25l");

    int cnt = sys_web_tab_count();
    if (cnt <= 0) {
        sys_web_tab_new("about:blank");
    }

    g_dirty = 1;
    render_page();

    for (;;) {
        char buf[32];
        int n = sys_read(0, buf, sizeof(buf));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                handle_key((unsigned char)buf[i]);
            }
        }
        if (g_dirty) {
            render_page();
        }
        sys_sleep(50);
    }

    return 0;
}
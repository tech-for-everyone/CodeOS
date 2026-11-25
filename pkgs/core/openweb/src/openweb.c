#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "openweb.h"
#include "ow_html.h"
#include "ow_engine.h"
#include "ow_url.h"
#include <unistd.h>

#define MAX_URL 512
#define MAX_LINES 200
#define SCROLL_STEP 10

static char g_url[MAX_URL] = "";
static int  g_scroll = 0;
static int  g_dirty = 1;
static int  g_show_help = 0;
static char g_status_msg[256] = "";

/* Raw mode makes sys_read() hand back each keystroke as it arrives instead of
 * buffering until Enter, which is what the single-key hotkeys in handle_key()
 * are written for. Enter was previously required after every hotkey. The
 * kernel stores this per-process, so it cannot outlive us and strand the shell
 * in raw mode; restoring on exit is hygiene, and keeps the app correct if the
 * tty mode is ever made shared the way a real terminal's is. */
static termios_t g_saved_tio;
static int g_tio_saved = 0;

static int enter_raw_mode(void) {
    if (tcgetattr(0, &g_saved_tio) != 0) return 0;
    g_tio_saved = 1;
    termios_t raw = g_saved_tio;
    raw.c_lflag &= ~(unsigned int)ICANON;
    if (tcsetattr(0, TCSANOW, &raw) != 0) { g_tio_saved = 0; return 0; }
    return 1;
}

static void quit(void) {
    if (g_tio_saved) tcsetattr(0, TCSANOW, &g_saved_tio);
    /* Re-show the cursor hidden at startup. */
    printf("\x1b[?25h");
    sys_exit(0);
}

static void clear_screen(void) {
    printf("\x1b[2J");
}

/* Page URLs come from the shared backend, not from the address field: the
 * address field may be empty while a page is displayed.  Links in the text
 * grid are relative to that URL, so both a link follow and a reload need it. */
static void current_url(char *buf, size_t size) {
    web_state_t st;
    buf[0] = 0;
    memset(&st, 0, sizeof(st));
    if (sys_web_get_info(&st) < 0) return;
    ow_url_copy(buf, size, st.url, sizeof(st.url));
}

static int max_scroll(void) {
    int max = ow_txt_lines - MAX_LINES;
    return max > 0 ? max : 0;
}

static void clamp_scroll(void) {
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > max_scroll()) g_scroll = max_scroll();
}

static void goto_home(void) {
    printf("\x1b[H");
}

/* OpenWeb's text renderer carries semantic line types; use them so headings,
 * quotes, tables and rules read as structure instead of one flat wall. */
static const char *line_style(uint8_t type) {
    switch (type) {
        case OW_LT_H1: case OW_LT_H2: case OW_LT_H3:
            return "\x1b[1;96m";
        case OW_LT_H4: case OW_LT_H5: case OW_LT_H6:
            return "\x1b[1;95m";
        case OW_LT_HR:
            return "\x1b[90m";
        case OW_LT_LI:
            return "\x1b[92m";
        case OW_LT_TH:
            return "\x1b[1;93m";
        case OW_LT_TD:
            return "\x1b[37m";
        case OW_LT_BQ:
            return "\x1b[3;94m";
        case OW_LT_BOLD:
            return "\x1b[1m";
        case OW_LT_ITALIC:
            return "\x1b[3m";
        case OW_LT_CODE:
            return "\x1b[96m";
        default:
            return "";
    }
}

static void render_help(void) {
    clear_screen();
    goto_home();

    printf("\x1b[1mOpenWeb Help\x1b[0m \x1b[36mYour web, shaped by you.\x1b[0m\n");
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
    printf("  \x1b[1ma\x1b[0m       Toggle AdBlock (kernel enforcement)\n");
    printf("  \x1b[1mq\x1b[0m      Quit\n");
    printf("  \x1b[1mEsc\x1b[0m      Exit app\n");
    printf("  \x1b[1m?\x1b[0m       Show/hide help\n");
    printf("\x1b[33mStatus:\x1b[0m\n");
    if (g_status_msg[0]) {
        printf("  %s\n", g_status_msg);
    }
    printf("\x1b[33mTips:\x1b[0m\n");
    printf("  \x1b[1m1-9, Enter\x1b[0m Follow the numbered link on a page;\n");
    printf("                 with no links, switch to that tab\n");
    printf("  \x1b[1mH/L\x1b[0m       Toggle help\n");
    g_dirty = 0;
}

static void render_adblock(void) {
    web_adblock_info_t info;
    memset(&info, 0, sizeof(info));
    if (sys_web_adblock_status(&info) != 0) {
        printf(" \x1b[90mAdBlock n/a\x1b[0m");
        return;
    }
    printf(" \x1b[%smAdBlock %s (%d domains)\x1b[0m",
           info.enabled ? "32" : "90",
           info.enabled ? "on" : "off", info.domains);
}

static void render_page(void) {
    int active = sys_web_tab_active_get();
    if (active < 0) active = 0;
    if (sys_web_tab_set_active(active) < 0) return;

    web_state_t st;
    memset(&st, 0, sizeof(st));
    if (sys_web_get_info(&st) < 0) return;

    /* One spare byte for the terminator: the backend can return a body of
     * exactly OW_CONTENT_MAX bytes. */
    char content[OW_CONTENT_MAX + 1];
    int clen = st.content_len;
    int rendered = 0;
    if (clen > 0 && clen <= OW_CONTENT_MAX) {
        int n = sys_web_get_content(content, clen);
        if (n > 0) {
            content[n] = 0;
            render_html(content, n);
            rendered = 1;
        }
    }
    /* A new tab, an empty document or a failed load must not keep showing the
     * previous page's text grid and numbered links. */
    if (!rendered) render_html("", 0);

    clear_screen();
    goto_home();

    printf("\x1b[1mOpenWeb 2.1\x1b[0m");
    printf(" \x1b[90m%c%d%c\x1b[0m", active < 0 ? '[' : '(', active + 1, active < 0 ? ']' : ')');
    printf(" \x1b[90m%d tabs\x1b[0m", sys_web_tab_count());
    render_adblock();
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
            const char *style = line_style(ow_line_info[i].type);
            printf("  ");
            if (link_idx >= 0) {
                printf("\x1b[34m[%d]\x1b[0m ", link_idx + 1);
            }
            printf("%s%s\x1b[0m\n", style, ow_txt[i]);
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

    printf("\x1b[2m--- Help: ? Show/Hide  a AdBlock  u/d Scroll  Tab Switch  Enter Navigate  N,Enter Follow Link  Esc Quit ---\x1b[0m");
    g_dirty = 0;
}

static void handle_key(int key) {
    if (key == '?') {
        g_show_help = !g_show_help;
        g_dirty = 1;
        return;
    }
    if (key == '\x1b') {
        quit();
    }
    if (key == '\n' || key == '\r') {
        char base[MAX_URL], url[MAX_URL];
        if (strlen(g_url) == 0) return;

        if (ow_url_all_digits(g_url)) {
            int idx = atoi(g_url) - 1;
            g_url[0] = 0;
            /* A number means "follow this link" while viewing a page with
             * numbered links.  On a page without links it keeps its old
             * meaning: switch to that tab by number. */
            if (idx >= 0 && idx < ow_link_cnt && ow_links[idx].url[0]) {
                current_url(base, sizeof(base));
                ow_url_resolve(base, ow_links[idx].url, url, sizeof(url));
                snprintf(g_status_msg, sizeof(g_status_msg), "Following link %d: %.*s",
                         idx + 1, (int)sizeof(g_status_msg) - 22, url);
                sys_web_navigate(url);
                g_scroll = 0;
            } else if (idx >= 0 && idx < sys_web_tab_count()) {
                sys_web_tab_set_active(idx);
                snprintf(g_status_msg, sizeof(g_status_msg), "Switched to tab %d", idx + 1);
            } else {
                snprintf(g_status_msg, sizeof(g_status_msg),
                         "No link or tab %d", idx + 1);
            }
            g_dirty = 1;
            return;
        }

        ow_url_trim_copy(url, sizeof(url), g_url);
        if (!url[0]) { g_url[0] = 0; return; }
        snprintf(g_status_msg, sizeof(g_status_msg), "Navigating to: %.*s",
                 (int)sizeof(g_status_msg) - 16, url);
        sys_web_navigate(url);
        g_url[0] = 0;
        g_scroll = 0;
        g_dirty = 1;
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
    /* While the address field has text, ordinary letters belong to the URL.
     * Previously keys such as t, b, r and s always triggered browser actions,
     * making many hostnames impossible to type. */
    if (g_url[0] == 0 && (key == 't' || key == 'T')) {
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
    if (g_url[0] == 0 && (key == 'c' || key == 'C' || key == 'x' || key == 'X')) {
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
    if (g_url[0] == 0 && (key == 'q' || key == 'Q')) {
        quit();
    }
    if (g_url[0] == 0 && (key == 'r' || key == 'R')) {
        if (g_url[0]) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Reloading: %.*s",
                     (int)sizeof(g_status_msg) - 12, g_url);
            sys_web_navigate(g_url);
        } else {
            web_state_t st;
            memset(&st, 0, sizeof(st));
            sys_web_get_info(&st);
            if (st.url[0]) {
                snprintf(g_status_msg, sizeof(g_status_msg), "Reloading: %.*s",
                         (int)sizeof(g_status_msg) - 12, st.url);
                sys_web_navigate(st.url);
            } else {
                snprintf(g_status_msg, sizeof(g_status_msg), "No URL to reload");
            }
        }
        g_dirty = 1;
        return;
    }
    if (g_url[0] == 0 && (key == 'u' || key == 'U' || key == 'p' || key == 'P')) {
        if (key == 'p' || key == 'P') {
            g_scroll = 0;
        } else {
            g_scroll = g_scroll > 0 ? g_scroll - SCROLL_STEP : 0;
        }
        clamp_scroll();
        snprintf(g_status_msg, sizeof(g_status_msg), "Scroll position: %d", g_scroll);
        g_dirty = 1;
        return;
    }
    if (g_url[0] == 0 && (key == 'd' || key == 'D' || key == 'n' || key == 'N')) {
        if (key == 'n' || key == 'N') {
            g_scroll = max_scroll();
        } else {
            g_scroll += SCROLL_STEP;
        }
        clamp_scroll();
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
    }
    if (g_url[0] == 0 && (key == 'h' || key == 'H' || key == 'l' || key == 'L')) {
        g_show_help = (key == 'h' || key == 'H');
        snprintf(g_status_msg, sizeof(g_status_msg), "%s help", g_show_help ? "Showing" : "Hiding");
        g_dirty = 1;
        return;
    }
    if (g_url[0] == 0 && (key == 'b' || key == 'B')) {
        sys_web_go_back();
        snprintf(g_status_msg, sizeof(g_status_msg), "Back (history)");
        g_dirty = 1;
        return;
    }
    if (g_url[0] == 0 && (key == 'f' || key == 'F')) {
        sys_web_go_forward();
        snprintf(g_status_msg, sizeof(g_status_msg), "Forward (history)");
        g_dirty = 1;
        return;
    }
    if (g_url[0] == 0 && (key == 's' || key == 'S')) {
        sys_web_stop();
        snprintf(g_status_msg, sizeof(g_status_msg), "Stop loading");
        g_dirty = 1;
        return;
    }
    if (g_url[0] == 0 && (key == 'a' || key == 'A')) {
        web_adblock_info_t info;
        memset(&info, 0, sizeof(info));
        if (sys_web_adblock_status(&info) != 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "AdBlock control unavailable");
        } else if (sys_web_adblock_set(!info.enabled) != 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "AdBlock change failed");
        } else if (sys_web_adblock_status(&info) != 0) {
            snprintf(g_status_msg, sizeof(g_status_msg), "AdBlock changed; status unavailable");
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "AdBlock: %s (%d domains)",
                     info.enabled ? "on" : "off", info.domains);
        }
        g_url[0] = 0;
        g_dirty = 1;
        return;
    }
    if (key >= ' ' && key <= '~') {
        int len = strlen(g_url);
        if (len < MAX_URL - 1) {
            g_url[len] = (char)key;
            g_url[len + 1] = 0;
            snprintf(g_status_msg, sizeof(g_status_msg), "URL: %.*s",
                     (int)sizeof(g_status_msg) - 6, g_url);
            g_dirty = 1;
        }
    }
}

int main(void) {
    ow_engine_init();
    printf("\x1b[?25l");
    int raw = enter_raw_mode();

    int cnt = sys_web_tab_count();
    if (cnt <= 0) {
        sys_web_tab_new("about:blank");
    }

    if (raw) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Raw mode on (no Enter needed)");
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "Raw mode unavailable (press Enter after each key)");
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

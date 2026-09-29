/* android-calculator — CodeOS Android app: integer calculator.
 *
 * Keypad + expression display; evaluates with precedence. GUI and console
 * modes. In console mode a line-based REPL is offered via stdin if stdout
 * is attached (headless tests type 'help').
 */

#include "stdio.h"
#include "string.h"
#include "android_ui.h"

#define EXPR_MAX 96

static char expr[EXPR_MAX];
static int  expr_len;

static void expr_reset(void) { expr[0] = 0; expr_len = 0; }

static void expr_push(char c) {
    if (expr_len < EXPR_MAX - 1) {
        expr[expr_len++] = c;
        expr[expr_len] = 0;
    }
}

static void expr_back(void) {
    if (expr_len > 0) { expr_len--; expr[expr_len] = 0; }
}

/* ── expression evaluator (integer, precedence-aware) ── */

typedef struct { const char *s; int pos; } ep_t;

static long long ep_expr(ep_t *e); /* forward (expression → term chain) */

static long long ep_num(ep_t *e) {
    int neg = 0;
    if (e->s[e->pos] == '-') { neg = 1; e->pos++; }
    long long v = 0;
    if (e->s[e->pos] == '(') {
        e->pos++;
        v = ep_expr(e);
        if (e->s[e->pos] == ')') e->pos++;
        return neg ? -v : v;
    }
    int any = 0;
    while (e->s[e->pos] >= '0' && e->s[e->pos] <= '9') {
        v = v * 10 + (e->s[e->pos] - '0');
        e->pos++;
        any = 1;
    }
    if (!any) return 0;
    return neg ? -v : v;
}

static long long ep_factor(ep_t *e) {
    long long v = ep_num(e);
    while (e->s[e->pos] == '*') {
        e->pos++;
        long long r = ep_num(e);
        v = v * r;
    }
    return v;
}

static long long ep_term(ep_t *e) {
    long long v = ep_factor(e);
    while (e->s[e->pos] == '/' || e->s[e->pos] == '%') {
        char op = e->s[e->pos++];
        long long r = ep_factor(e);
        if (r == 0) { v = 0; continue; }
        v = (op == '/') ? v / r : v % r;
    }
    return v;
}

long long ep_expr(ep_t *e) {
    long long v = ep_term(e);
    while (e->s[e->pos] == '+' || e->s[e->pos] == '-') {
        char op = e->s[e->pos++];
        long long r = ep_term(e);
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

static char *eval_result(char *out, int out_sz) {
    ep_t e = { expr, 0 };
    long long v = ep_expr(&e);
    snprintf(out, out_sz, "%lld", v);
    return out;
}

/* ── keypad layout ── */

#define KX 10
#define KY 120
#define KW 70
#define KH 60
#define KG 8

static const char *keys[6][4] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"0", ".", "C", "+"},
    {"(", ")", "%", "="},
    {"DEL", "ACK", "RUN", "OFF"},
};

static void draw_keypad(au_app_t *a) {
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            const char *k = keys[r][c];
            int x = KX + c * (KW + KG);
            int y = KY + r * (KH + KG);
            int is_op = (k[0] >= '*' && k[0] <= '/') || k[0] == '%' ||
                        (k[0] == '=' ) || (k[0] == '(' ) || k[0] == ')' ||
                        (k[0] == 'C') || (k[0] == '-');
            au_button(a, x, y, KW, KH, k, is_op ? AU_PRIMARY : AU_SURFACE,
                      is_op ? AU_ON_PRIMARY : AU_TEXT);
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int console_only = argc > 1 && strcmp(argv[1], "--console-only") == 0;

    expr_reset();
    au_app_t *a = au_start("Calculator", AU_W_DEFAULT, AU_H_DEFAULT);
    if (console_only && !au_console_mode(a)) { au_end(a); return 0; }

    int shown_hint = 0;

    while (1) {
        if (a->wm == AU_WM_OK) {
            au_clear(a);
            au_statusbar(a, "Calculator", -1, -1);

            /* display */
            au_rect(a, 10, 40, a->width - 20, 60, AU_SURFACE2);
            char disp[EXPR_MAX + 16];
            if (expr_len == 0) snprintf(disp, sizeof(disp), "0");
            else if (expr[expr_len - 1] == '=') {
                snprintf(disp, sizeof(disp), "%s", expr);
            } else {
                snprintf(disp, sizeof(disp), "%s", expr);
            }
            au_text(a, 20, 58, AU_TEXT, disp);

            draw_keypad(a);
            au_flush(a);
        } else if (!shown_hint) {
            puts("android-calculator: keys: digits + - * / % ( ) . '=' evaluates, 'C' clears");
            puts("  type an expression and press Enter; 'q' quits.");
            shown_hint = 1;
        }

        int key = 0, mx = 0, my = 0, mb = 0;
        int ev = au_poll(a, &key, &mx, &my, &mb);
        if (ev == WM_EVENT_CLOSED) break;

        if (ev == WM_EVENT_KEY) {
            if (key == 'q' || key == 27) break;
            if (key == '\n' || key == '\r') {
                char v[32];
                snprintf(expr + expr_len, sizeof(expr) - expr_len - 1, "=");
                expr_len++;
                eval_result(v, sizeof(v));
                if (a->wm == AU_WM_OK) {
                    char d[EXPR_MAX + 8];
                    snprintf(d, sizeof(d), "%s=%s", expr, v);
                    expr_reset();
                    expr_push('=');
                    for (int i = 0; v[i] && expr_len < EXPR_MAX - 2 && i < 24; i++)
                        expr_push(v[i]);
                } else {
                    printf("[calc] %s = %s\n", expr, v);
                    expr_reset();
                }
                continue;
            }
            if (key == '\b' || key == 127) { expr_back(); continue; }
            char c = (char)key;
            if (strchr("0123456789+-*/().%", c)) {
                if (expr_len == 1 && expr[0] == '=') expr_reset();
                expr_push(c);
            } else if (c == 'c' || c == 'C') {
                expr_reset();
            } else if (c == ' ') {
                char v[32];
                eval_result(v, sizeof(v));
                printf("[calc] = %s\n", v);
            }
        } else if (ev == WM_EVENT_MOUSE && mb) {
            /* map click into keypad grid */
            int c = (mx - KX) / (KW + KG);
            int r = (my - KY) / (KH + KG);
            if (r >= 0 && r < 6 && c >= 0 && c < 4 && keys[r][c][0]) {
                const char *k = keys[r][c];
                if (k[0] == '=') {
                    char v[32];
                    snprintf(expr + expr_len, sizeof(expr) - expr_len - 1, "=");
                    expr_len++;
                    eval_result(v, sizeof(v));
                    printf("[calc] %s = %s\n", expr, v);
                    char d[EXPR_MAX + 8];
                    snprintf(d, sizeof(d), "%s", v);
                    expr_reset();
                    expr_push('=');
                    for (int i = 0; v[i] && i < 24; i++) expr_push(v[i]);
                } else if (k[0] == 'C') {
                    expr_reset();
                } else if (k[0] < '0' || k[0] > '9') {
                    if (k[0] == 'd' || k[0] == 'D') { /* DEL row 5 */ }
                    else expr_push(k[0]);
                } else {
                    expr_push(k[0]);
                }
            }
        }

        if (a->wm != AU_WM_OK) {
            /* console REPL */
            char buf[128];
            int n = 0;
            int got = 0;
            while (n < 120) {
                char ch;
                int r = sys_read(0, &ch, 1);
                if (r <= 0) break;
                if (ch == '\n' || ch == '\r') { got = 1; break; }
                buf[n++] = ch;
            }
            if (got) {
                buf[n] = 0;
                if (strcmp(buf, "q") == 0) break;
                if (strcmp(buf, "c") == 0) { expr_reset(); continue; }
                expr_reset();
                for (int i = 0; buf[i] && i < EXPR_MAX - 2; i++) expr_push(buf[i]);
                char v[32];
                eval_result(v, sizeof(v));
                printf("[calc] %s = %s\n", buf, v);
                continue;
            }
            if (n <= 0) { sys_sleep(100); a->frame++; if (a->frame > 200) break; }
        } else {
            sys_sleep(16);
        }
    }

    au_end(a);
    return 0;
}
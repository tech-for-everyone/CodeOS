#include "calc.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"
#include "fb.h"
#include "keyboard.h"

#define CALC_WIN_W  300
#define CALC_WIN_H  420
#define CALC_DISP_H 80
#define CALC_GAP    4
#define CALC_COLS   4
#define CALC_ROWS   6

#define C_CALC_BG     C_CRUST
#define C_CALC_DISP   C_BASE
#define C_CALC_NUM    C_SURFACE0
#define C_CALC_OP     C_SURFACE1
#define C_CALC_EQ     C_BLUE
#define C_CALC_FUNC   C_SURFACE2
#define C_CALC_TEXT   C_TEXT
#define C_CALC_EQTEXT C_TEXT
#define C_CALC_DIM    C_OVERLAY0

typedef struct { int r, c, span; const char *l; uint32_t bg; } cb_t;

static const cb_t cb[] = {
    {0,0,1,"C",   C_CALC_FUNC}, {0,1,1,"(",  C_CALC_OP}, {0,2,1,")", C_CALC_OP}, {0,3,1,"/", C_CALC_OP},
    {1,0,1,"7",   C_CALC_NUM},  {1,1,1,"8",  C_CALC_NUM}, {1,2,1,"9",C_CALC_NUM},  {1,3,1,"*", C_CALC_OP},
    {2,0,1,"4",   C_CALC_NUM},  {2,1,1,"5",  C_CALC_NUM}, {2,2,1,"6",C_CALC_NUM},  {2,3,1,"-", C_CALC_OP},
    {3,0,1,"1",   C_CALC_NUM},  {3,1,1,"2",  C_CALC_NUM}, {3,2,1,"3",C_CALC_NUM},  {3,3,1,"+", C_CALC_OP},
    {4,0,1,".",   C_CALC_NUM},  {4,1,1,"0",  C_CALC_NUM}, {4,2,1,"\xb1",C_CALC_FUNC},{4,3,1,"=",C_CALC_EQ},
};
#define CB_COUNT (sizeof(cb) / sizeof(cb[0]))

static int c_open, c_win;
static char c_disp[40];
static char c_expr_str[64];

#define EXPR_MAX 128
static char c_expr[EXPR_MAX];
static int c_expr_len;
static int c_has_result;
static int calc_hover_r = -1, calc_hover_c = -1;

static uint32_t calc_brighten(uint32_t col, int amt) {
    int r = (col >> 16) & 0xFF;
    int g = (col >> 8) & 0xFF;
    int b = col & 0xFF;
    r += ((255 - r) * amt) / 255;
    g += ((255 - g) * amt) / 255;
    b += ((255 - b) * amt) / 255;
    return (col & 0xFF000000) | (r << 16) | (g << 8) | b;
}

/* Parser state for fixed-point integer expression evaluation.
 * All values are stored as fixed-point with 3 decimal places (x1000).
 * So 1.5 is stored as 1500, 3.14 as 3140, etc. */
typedef struct {
    const char *s;
    int pos;
    int err;
} parser_t;

static void parser_skip_spaces(parser_t *p) {
    while (p->s[p->pos] == ' ') p->pos++;
}

static int parse_number(parser_t *p) {
    parser_skip_spaces(p);
    int neg = 0;
    if (p->s[p->pos] == '-') { neg = 1; p->pos++; }
    else if (p->s[p->pos] == '+') { p->pos++; }

    int val = 0;
    int has_digit = 0;
    while (p->s[p->pos] >= '0' && p->s[p->pos] <= '9') {
        val = val * 10 + (p->s[p->pos] - '0');
        p->pos++;
        has_digit = 1;
    }
    if (!has_digit && p->s[p->pos] != '.') { p->err = 1; return 0; }

    if (p->s[p->pos] == '.') {
        p->pos++;
        int frac = 0;
        int place = 100;
        int digits = 0;
        while (p->s[p->pos] >= '0' && p->s[p->pos] <= '9' && digits < 3) {
            frac += (p->s[p->pos] - '0') * place;
            place /= 10;
            digits++;
            p->pos++;
        }
        val = val * 1000 + frac;
    } else {
        val *= 1000;
    }
    return neg ? -val : val;
}

static int parse_factor(parser_t *p);
static int parse_term(parser_t *p);
static int parse_expression(parser_t *p);

static int parse_factor(parser_t *p) {
    parser_skip_spaces(p);
    if (p->err) return 0;

    int ch = p->s[p->pos];
    if (ch == '(') {
        p->pos++;
        int val = parse_expression(p);
        parser_skip_spaces(p);
        if (p->s[p->pos] == ')') p->pos++;
        return val;
    }
    if (ch == '-' || ch == '+') {
        p->pos++;
        int val = parse_factor(p);
        return (ch == '-') ? -val : val;
    }
    return parse_number(p);
}

static int parse_term(parser_t *p) {
    int left = parse_factor(p);
    while (!p->err) {
        parser_skip_spaces(p);
        int ch = p->s[p->pos];
        if (ch == '*' || ch == '/') {
            p->pos++;
            int right = parse_factor(p);
            if (ch == '*') {
                left = (int)(((long long)left * (long long)right) / 1000);
            } else {
                if (right == 0) { p->err = 1; return 0; }
                left = (int)(((long long)left * 1000) / (long long)right);
            }
        } else break;
    }
    return left;
}

static int parse_expression(parser_t *p) {
    int left = parse_term(p);
    while (!p->err) {
        parser_skip_spaces(p);
        int ch = p->s[p->pos];
        if (ch == '+' || ch == '-') {
            p->pos++;
            int right = parse_term(p);
            if (ch == '+') left += right;
            else left -= right;
        } else break;
    }
    return left;
}

static int c_eval_expr(const char *expr, int *err) {
    parser_t p;
    p.s = expr;
    p.pos = 0;
    p.err = 0;
    int result = parse_expression(&p);
    *err = p.err;
    return result;
}

static void c_format_fixed(int val_fixed, char *buf, int bufsz) {
    int neg = 0;
    if (val_fixed < 0) { neg = 1; val_fixed = -val_fixed; }
    int intpart = val_fixed / 1000;
    int frac = val_fixed % 1000;
    if (frac < 0) frac = -frac;

    char tmp[16];
    int ti = 0;
    if (intpart == 0) tmp[ti++] = '0';
    else {
        int rv[10];
        int ri = 0;
        int v2 = intpart;
        while (v2 > 0) { rv[ri++] = '0' + v2 % 10; v2 /= 10; }
        while (ri > 0) tmp[ti++] = rv[--ri];
    }
    int di = 0;
    if (neg && bufsz > 1) buf[di++] = '-';
    for (int i = 0; i < ti && di < bufsz - 1; i++) buf[di++] = tmp[i];
    if (frac > 0 && di < bufsz - 8) {
        buf[di++] = '.';
        buf[di++] = '0' + (frac / 100) % 10;
        buf[di++] = '0' + (frac / 10) % 10;
        buf[di++] = '0' + frac % 10;
        while (di > 1 && buf[di - 1] == '0' && buf[di - 2] != '.') di--;
    }
    buf[di] = 0;
}

static void c_upd_disp(void) {
    if (c_has_result && c_expr_len > 0) {
        int err = 0;
        int val = c_eval_expr(c_expr, &err);
        if (!err) {
            c_format_fixed(val, c_disp, sizeof(c_disp));
        } else {
            strlcpy(c_disp, "Error", sizeof(c_disp));
        }
    } else if (c_expr_len > 0) {
        strlcpy(c_disp, c_expr, sizeof(c_disp));
    } else {
        strlcpy(c_disp, "0", sizeof(c_disp));
    }
    strlcpy(c_expr_str, c_expr, sizeof(c_expr_str));
}

static void c_clr_all(void) {
    c_expr[0] = 0;
    c_expr_len = 0;
    c_has_result = 0;
    c_upd_disp();
}

static void c_input_char(char ch) {
    if (c_has_result) {
        if ((ch >= '0' && ch <= '9') || ch == '.') {
            c_expr[0] = ch;
            c_expr[1] = 0;
            c_expr_len = 1;
            c_has_result = 0;
        } else {
            c_has_result = 0;
        }
    } else {
        if (c_expr_len < EXPR_MAX - 2) {
            c_expr[c_expr_len++] = ch;
            c_expr[c_expr_len] = 0;
        }
    }
    c_upd_disp();
}

static void c_backspace(void) {
    if (c_has_result) { c_clr_all(); return; }
    if (c_expr_len > 0) {
        c_expr_len--;
        c_expr[c_expr_len] = 0;
    }
    c_upd_disp();
}

void calc_open(void) {
    if (c_open) return;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    c_win = desktop_new_window((sw - CALC_WIN_W) / 2, (sh - CALC_WIN_H) / 3,
                                CALC_WIN_W, CALC_WIN_H, "Calculator", 0xFFFFFFFF, C_CALC_BG);
    if (c_win < 0) return;
    c_open = 1;
    c_clr_all();
    desktop_redraw();
}

int calc_is_open(void) { return c_open; }
void calc_close(void) { c_open = 0; calc_hover_r = -1; calc_hover_c = -1; }

void calc_mousemove(int mx, int my) {
    if (!c_open) { calc_hover_r = -1; calc_hover_c = -1; return; }
    window_t *w = desktop_get_window(c_win);
    if (!w || !w->visible) { calc_hover_r = -1; calc_hover_c = -1; return; }
    int cx = w->x + 4, cy = w->y + 22, cw = w->w - 8, ch = w->h - 26;
    int by = cy + CALC_DISP_H + CALC_GAP + 2;
    int bh = (ch - CALC_DISP_H - 8 - CALC_GAP * (CALC_ROWS + 1)) / CALC_ROWS;
    if (bh < 20) bh = 20;
    int b1w = (cw - CALC_GAP * (CALC_COLS + 1)) / CALC_COLS;

    for (unsigned i = 0; i < CB_COUNT; i++) {
        if (!cb[i].l[0]) continue;
        int bx2 = cx + CALC_GAP + cb[i].c * (b1w + CALC_GAP);
        int bw = cb[i].span > 1 ? b1w * cb[i].span + CALC_GAP * (cb[i].span - 1) : b1w;
        int by2 = by + cb[i].r * (bh + CALC_GAP);
        if (mx >= bx2 && mx < bx2 + bw && my >= by2 && my < by2 + bh) {
            if (calc_hover_r != (int)cb[i].r || calc_hover_c != (int)cb[i].c) {
                calc_hover_r = cb[i].r;
                calc_hover_c = cb[i].c;
                desktop_redraw();
            }
            return;
        }
    }
    if (calc_hover_r != -1 || calc_hover_c != -1) {
        calc_hover_r = -1;
        calc_hover_c = -1;
        desktop_redraw();
    }
}

void calc_draw(void) {
    if (!c_open) return;
    window_t *w = desktop_get_window(c_win);
    if (!w || !w->visible) return;
    int cx = w->x + 4, cy = w->y + 22, cw = w->w - 8, ch = w->h - 26;
    if (cw < 20 || ch < 20) return;
    fb_fillrect(cx, cy, cw, ch, C_CALC_BG);
    fb_fillrect(cx, cy, cw, 1, C_GLASS_BORDER);

    fb_fillrect(cx + 2, cy + 2, cw - 4, CALC_DISP_H, C_CALC_DISP);
    fb_draw_rounded_rect(cx + 2, cy + 2, cw - 4, CALC_DISP_H, 6, C_GLASS_BORDER);

    if (c_expr_str[0]) {
        int el = strlen(c_expr_str);
        int max_chars = (cw - 20) / 8;
        const char *show = c_expr_str;
        if (el > max_chars) show = c_expr_str + el - max_chars;
        fb_drawstr_px(cx + cw - 10 - (int)strlen(show) * 8, cy + 8, show, C_CALC_DIM, C_CALC_DISP);
    }

    int dl = strlen(c_disp);
    int max_chars_d = (cw - 20) / 8;
    const char *show_d = c_disp;
    if (dl > max_chars_d) { show_d = c_disp + dl - max_chars_d; dl = max_chars_d; }
    fb_drawstr_px(cx + cw - 10 - dl * 8, cy + CALC_DISP_H - 22, show_d, C_CALC_TEXT, C_CALC_DISP);

    int by = cy + CALC_DISP_H + CALC_GAP + 2;
    int bh = (ch - CALC_DISP_H - 8 - CALC_GAP * (CALC_ROWS + 1)) / CALC_ROWS;
    if (bh < 20) bh = 20;
    int b1w = (cw - CALC_GAP * (CALC_COLS + 1)) / CALC_COLS;

    for (unsigned i = 0; i < CB_COUNT; i++) {
        if (!cb[i].l[0]) continue;
        int bx2 = cx + CALC_GAP + cb[i].c * (b1w + CALC_GAP);
        int bw = cb[i].span > 1 ? b1w * cb[i].span + CALC_GAP * (cb[i].span - 1) : b1w;
        int by2 = by + cb[i].r * (bh + CALC_GAP);
        uint32_t bg = cb[i].bg;
        uint32_t fg = (bg == C_CALC_EQ) ? C_CALC_EQTEXT : C_CALC_TEXT;
        int hovered = (calc_hover_r == (int)cb[i].r && calc_hover_c == (int)cb[i].c);

        if (hovered) {
            fb_fill_rounded_rect_gradient_v(bx2, by2, bw, bh, 8, calc_brighten(bg, 35), bg);
        } else {
            fb_fill_rounded_rect(bx2, by2, bw, bh, 8, bg);
        }
        fb_draw_rounded_rect(bx2, by2, bw, bh, 8, C_GLASS_BORDER);
        int tw2 = (int)strlen(cb[i].l) * 8;
        fb_drawstr_px(bx2 + (bw - tw2) / 2, by2 + (bh - 16) / 2, cb[i].l, fg, bg);
    }
}

int calc_click(int mx, int my) {
    if (!c_open) return 0;
    window_t *w = desktop_get_window(c_win);
    if (!w || !w->visible) return 0;
    desktop_set_focused(c_win);
    int cx = w->x + 4, cy = w->y + 22, cw = w->w - 8, ch = w->h - 26;
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return 0;

    int by = cy + CALC_DISP_H + CALC_GAP + 2;
    int bh = (ch - CALC_DISP_H - 8 - CALC_GAP * (CALC_ROWS + 1)) / CALC_ROWS;
    if (bh < 20) bh = 20;
    int b1w = (cw - CALC_GAP * (CALC_COLS + 1)) / CALC_COLS;

    for (unsigned i = 0; i < CB_COUNT; i++) {
        if (!cb[i].l[0]) continue;
        int bx2 = cx + CALC_GAP + cb[i].c * (b1w + CALC_GAP);
        int bw = cb[i].span > 1 ? b1w * cb[i].span + CALC_GAP * (cb[i].span - 1) : b1w;
        int by2 = by + cb[i].r * (bh + CALC_GAP);
        if (mx >= bx2 && mx < bx2 + bw && my >= by2 && my < by2 + bh) {
            char ch2 = cb[i].l[0];
            if (ch2 >= '0' && ch2 <= '9') c_input_char(ch2);
            else if (ch2 == '(' || ch2 == ')') c_input_char(ch2);
            else if (ch2 == '+' || ch2 == '-' || ch2 == '*' || ch2 == '/') c_input_char(ch2);
            else if (ch2 == '.') c_input_char('.');
            else if (ch2 == '=') { c_has_result = 1; c_upd_disp(); }
            else if (ch2 == 'C') c_clr_all();
            else if (ch2 == '\xb1') {
                if (c_expr_len > 0 && c_expr[0] == '-') {
                    for (int j = 0; j < c_expr_len; j++) c_expr[j] = c_expr[j + 1];
                    c_expr_len--;
                } else if (c_expr_len < EXPR_MAX - 1) {
                    for (int j = c_expr_len; j > 0; j--) c_expr[j] = c_expr[j - 1];
                    c_expr[0] = '-';
                    c_expr_len++;
                }
                c_expr[c_expr_len] = 0;
                c_upd_disp();
            }
            desktop_redraw();
            return 1;
        }
    }
    return 0;
}

void calc_key(int key) {
    if (!c_open) return;
    window_t *w = desktop_get_window(c_win);
    if (!w || !w->visible) return;
    if (desktop_focused_window() != c_win) return;

    int handled = 1;
    switch (key) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            c_input_char(key); break;
        case '+': case '-': case '*': case '/':
            c_input_char(key); break;
        case '.': c_input_char('.'); break;
        case '(': case ')': c_input_char(key); break;
        case '=': case '\n': case '\r':
            c_has_result = 1; c_upd_disp(); break;
        case '\b': c_backspace(); break;
        case 'c': case 'C': c_clr_all(); break;
        default: handled = 0; break;
    }
    if (handled) desktop_redraw();
}

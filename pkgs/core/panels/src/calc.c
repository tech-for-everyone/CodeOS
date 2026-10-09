#include "calc.h"
#include "jengine/jengine.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"

#define CALC_W 340
#define CALC_H 280

static int calc_open;
static char calc_expr[128];
static char calc_result[64];

void calc_open(void) {
    calc_open = 1;
    memset(calc_expr, 0, sizeof(calc_expr));
    memset(calc_result, 0, sizeof(calc_result));
}

void calc_close(void) {
    calc_open = 0;
}

int calc_is_open(void) { return calc_open; }

void calc_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!calc_open) return;

    int x = (scr_w - CALC_W) / 2;
    int y = (scr_h - CALC_H) / 2;

    /* Glass background */
    fb_fillrect_gradient_v(x, y, CALC_W, CALC_H,
                           0x302A2A2A, 0x201A1A1A);

    /* Title */
    fb_drawstr_px(x + 8, y + 6, "Calculator", C_BLUE, 0);

    /* Expression line */
    fb_drawstr_px(x + 8, y + 40, calc_expr, C_TEXT, 0);

    /* Result */
    fb_drawstr_px(x + 8, y + 70, calc_result, C_BLUE, 0);

    /* Buttons */
    int by = y + 100;
    int bh = 36;
    int bw = 56;
    int gap = 8;

    const char *buttons[] = {
        "C", "←", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "=", ""
    };

    for (int i = 0; i < 17; i++) {
        int bx = x + 8 + (i % 4) * (bw + gap);
        int byy = by + (i / 4) * (bh + gap);

        if (i == 16 && bx < x + CALC_W - 8) {
            bx = x + CALC_W - 8 - bw;
        }

        if (bx + bw > x + CALC_W - 8) continue;

        uint32_t btn_bg = 0x30FFFFFF;
        uint32_t btn_fg = C_TEXT;

        fb_fill_rounded_rect(bx, byy, bw, bh, 6, btn_bg);
        fb_drawstr_px(bx + 2, byy + 4, buttons[i], btn_fg, 0);
    }
}

int calc_click(int mx, int my) {
    int x = 0, y = 0; /* would get from scr_w/scr_h in real implementation */

    int x = 0; /* placeholder - actual implementation gets scr dimensions */
    int y = 0;

    /* Calculate window position - simplified */
    int win_x = (/* scr_w - CALC_W */) / 2;
    int win_y = (/* scr_h - CALC_H */) / 2 + 30;

    int by = win_y + 100;
    int bh = 36;
    int bw = 56;
    int gap = 8;

    for (int i = 0; i < 17; i++) {
        int bx = win_x + 8 + (i % 4) * (bw + gap);
        int byy = by + (i / 4) * (bh + gap);

        if (i == 16 && bx < win_x + CALC_W - 8) {
            bx = win_x + CALC_W - 8 - bw;
        }

        if (bx + bw > win_x + CALC_W - 8) continue;

        if (mx >= bx && mx < bx + bw && my >= byy && my < byy + bh) {
            if (i == 0) { /* C */
                memset(calc_expr, 0, sizeof(calc_expr));
                memset(calc_result, 0, sizeof(calc_result));
            } else if (i == 1) { /* ← */
                if (calc_expr[0]) {
                    int len = strlen(calc_expr);
                    calc_expr[len - 1] = 0;
                }
            } else if (i == 16) { /* = */
                /* Evaluate using Jengine */
                char jresult[64] = "";
                jengine_value result = jengine_eval(calc_expr);
                if (result.type == 0) { /* number */
                    snprintf(calc_result, sizeof(calc_result), "%lld",
                             (long long)result.num_val);
                } else if (result.type == 1) { /* string */
                    snprintf(calc_result, sizeof(calc_result), "%s",
                             result.str_val ? result.str_val : "");
                } else {
                    snprintf(calc_result, sizeof(calc_result), "error");
                }
            } else {
                if (strlen(calc_expr) < sizeof(calc_expr) - 1) {
                    calc_expr[strlen(calc_expr)] = buttons[i][0];
                    calc_expr[strlen(calc_expr) + 1] = 0;
                }
            }
            return 1;
        }
    }
    return 0;
}

void calc_mousemove(int mx, int my) {
    (void)mx; (void)my;
}

void calc_key(int key) {
    (void)key;
}
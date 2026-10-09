/* Zircon ARM64 mobile UI.
 *
 * The interactive replacement for the old static home-screen paint.  It owns:
 *   - the home screen (wallpaper + app grid + focus),
 *   - the status bar (clock, network, battery, Back),
 *   - navigation (launch an app, return home),
 *   - event dispatch from the ARM64 input layer (touch/pointer + keyboard).
 *
 * Everything is static storage; there is no allocator in this kernel.
 */
#include "ui.h"
#include "display.h"
#include "input.h"
#include "fb.h"
#include "rtc.h"
#include "kprintf.h"
#include "../kernel/types.h"

/* ────────────────────────────────────────────────────────────────────────
 * Framework state
 * ──────────────────────────────────────────────────────────────────────── */

#define MAX_APPS  8
#define STATUS_H  44

#define HOME_COLS 4
#define HOME_ICON 96
#define HOME_GAP  56

static ui_app_t *g_apps[MAX_APPS];
static int g_count;
static ui_app_t *g_active;
static int g_focus;
static int g_px = -1, g_py = -1;
static int g_dirty = 1;

void ui_register(ui_app_t *app) {
    if (g_count >= MAX_APPS) {
        kprintf("ui: registry full, dropping '%s'\n", app->name);
        return;
    }
    g_apps[g_count++] = app;
}

/* ────────────────────────────────────────────────────────────────────────
 * Home screen
 * ──────────────────────────────────────────────────────────────────────── */

static void home_cell(int i, int *x, int *y) {
    int total = HOME_COLS * HOME_ICON + (HOME_COLS - 1) * HOME_GAP;
    int x0 = ((int)fb_getwidth() - total) / 2;
    int y0 = STATUS_H + 56;
    int row = i / HOME_COLS, col = i % HOME_COLS;
    *x = x0 + col * (HOME_ICON + HOME_GAP);
    *y = y0 + row * (HOME_ICON + 56);
}

static int home_hit(int px, int py) {
    for (int i = 0; i < g_count; i++) {
        int x, y;
        home_cell(i, &x, &y);
        if (px >= x && px < x + HOME_ICON && py >= y && py < y + HOME_ICON)
            return i;
    }
    return -1;
}

static void draw_home(void) {
    display_wallpaper();
    draw_text_c((int)fb_getwidth() / 2, STATUS_H + 10, "Zircon",
                0xffffff, 0x21304d);
    for (int i = 0; i < g_count; i++) {
        int x, y;
        home_cell(i, &x, &y);
        display_rounded_rect((uint32_t)x, (uint32_t)y, HOME_ICON, HOME_ICON,
                             g_apps[i]->color);
        display_icon(g_apps[i]->icon, (uint32_t)(x + HOME_ICON / 2),
                     (uint32_t)(y + HOME_ICON / 2), HOME_ICON / 2, 0xffffff);
        draw_text_c(x + HOME_ICON / 2, y + HOME_ICON + 6, g_apps[i]->name,
                    0xe8edf5, 0x0d1826);
        if (i == g_focus)
            fb_drawrect((uint32_t)(x - 6), (uint32_t)(y - 6),
                        HOME_ICON + 12, HOME_ICON + 12, 0xffffff);
    }
}

/* ────────────────────────────────────────────────────────────────────────
 * Status bar
 * ──────────────────────────────────────────────────────────────────────── */

static int back_button_rect(int *bx, int *by, int *bw, int *bh) {
    *bx = 8; *by = 8; *bw = 56; *bh = 28;
    return 1;
}

static int hit_back(int px, int py) {
    int bx, by, bw, bh;
    back_button_rect(&bx, &by, &bw, &bh);
    return px >= bx && px < bx + bw && py >= by && py < by + bh;
}

static void draw_status(void) {
    uint32_t W = fb_getwidth();
    fb_fillrect(0, 0, (uint32_t)W, (uint32_t)STATUS_H, 0x0f1522);
    fb_fillrect(0, (uint32_t)STATUS_H, (uint32_t)W, 2, 0x2a3a5c);

    if (g_active) {
        int bx, by, bw, bh;
        back_button_rect(&bx, &by, &bw, &bh);
        fb_fillrect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh,
                    0x1b2740);
        display_icon(ICON_BACK, (uint32_t)(bx + 18), (uint32_t)(by + bh / 2),
                     9, 0xdfe6f2);
        fb_drawstr_px((uint32_t)(bx + bw + 12), 12, g_active->name,
                      0xffffff, 0x0f1522);
    } else {
        fb_drawstr_px(16, 12, "Zircon 1.0", 0xdfe6f2, 0x0f1522);
    }

    /* network bars + battery */
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 4;
        fb_fillrect(W - 96 - (uint32_t)i * 8, (uint32_t)(30 - h), 5, (uint32_t)h,
                    0x8fa3c8);
    }
    fb_drawrect(W - 56, 14, 24, 12, 0x8fa3c8);
    fb_fillrect(W - 30, 17, 3, 6, 0x8fa3c8);
    fb_fillrect(W - 54, 16, 18, 8, 0x4caf50);

    /* clock */
    uint32_t sod = rtc_read_wallclock() % 86400;
    char buf[8];
    buf[0] = (char)('0' + sod / 36000 % 10);
    buf[1] = (char)('0' + sod / 3600 % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + sod / 600 % 10);
    buf[4] = (char)('0' + sod / 60 % 10);
    buf[5] = 0;
    fb_drawstr_px(W - 116, 12, buf, 0xffffff, 0x0f1522);
}

/* ────────────────────────────────────────────────────────────────────────
 * Apps
 * ──────────────────────────────────────────────────────────────────────── */

/* -- Clock ------------------------------------------------------------- */

static const int clock_pts[60][2] = {
    {0,-1000}, {105,-995}, {208,-978}, {309,-951}, {407,-914}, {500,-866},
    {588,-809}, {669,-743}, {743,-669}, {809,-588}, {866,-500}, {914,-407},
    {951,-309}, {978,-208}, {995,-105}, {1000,0}, {995,105}, {978,208},
    {951,309}, {914,407}, {866,500}, {809,588}, {743,669}, {669,743},
    {588,809}, {500,866}, {407,914}, {309,951}, {208,978}, {105,995},
    {0,1000}, {-105,995}, {-208,978}, {-309,951}, {-407,914}, {-500,866},
    {-588,809}, {-669,743}, {-743,669}, {-809,588}, {-866,500}, {-914,407},
    {-951,309}, {-978,208}, {-995,105}, {-1000,0}, {-995,-105}, {-978,-208},
    {-951,-309}, {-914,-407}, {-866,-500}, {-809,-588}, {-743,-669}, {-669,-743},
    {-588,-809}, {-500,-866}, {-407,-914}, {-309,-951}, {-208,-978}, {-105,-995}
};

static void clock_hand(int cx, int cy, int r, int idx, uint32_t color, int thick) {
    int ex = cx + clock_pts[idx][0] * r / 1000;
    int ey = cy + clock_pts[idx][1] * r / 1000;
    for (int d = -(thick / 2); d <= thick / 2; d++) {
        fb_drawline(cx + d, cy, ex + d, ey, color);
        fb_drawline(cx, cy + d, ex, ey + d, color);
    }
}

static void clock_app_draw(ui_app_t *self, int x, int y, int w, int h) {
    (void)self;
    uint32_t floor_c = 0x0b1220;
    fb_fillrect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, floor_c);

    uint32_t t = rtc_read_wallclock();
    uint32_t sod = t % 86400;
    int hh = (int)(sod / 3600), mm = (int)(sod / 60 % 60), ss = (int)(sod % 60);
    int cx = x + w / 2;
    int cy = y + h / 2 - 40;
    int r = h / 2 - 90;
    if (r > 170) r = 170;
    if (r < 60) r = 60;

    circle_pts(cx, cy, r, 0x38507a);
    circle_pts(cx, cy, r - 1, 0x38507a);
    for (int i = 0; i < 60; i += 5) {
        int major = (i % 15) == 0;
        int outer = r - 8;
        int inner = outer - (major ? 16 : 8);
        fb_drawline(cx + clock_pts[i][0] * outer / 1000,
                    cy + clock_pts[i][1] * outer / 1000,
                    cx + clock_pts[i][0] * inner / 1000,
                    cy + clock_pts[i][1] * inner / 1000,
                    major ? 0xdfe6f2 : 0x6b7f9e);
    }
    clock_hand(cx, cy, r * 50 / 100, ((hh % 12) * 5 + mm / 12) % 60, 0xffffff, 4);
    clock_hand(cx, cy, r * 74 / 100, mm, 0xffffff, 3);
    clock_hand(cx, cy, r * 86 / 100, ss, 0xff5b5b, 1);
    fb_fillrect((uint32_t)(cx - 4), (uint32_t)(cy - 4), 9, 9, 0xffffff);

    char big[16];
    big[0] = (char)('0' + hh / 10); big[1] = (char)('0' + hh % 10);
    big[2] = ':'; big[3] = (char)('0' + mm / 10); big[4] = (char)('0' + mm % 10);
    big[5] = ':'; big[6] = (char)('0' + ss / 10); big[7] = (char)('0' + ss % 10);
    big[8] = 0;
    draw_text_c(cx, cy + r + 20, big, 0xffffff, floor_c);

    /* date */
    int64_t days = (int64_t)t / 86400;
    int wday = (int)((days + 4) % 7);
    static const char *wd[7] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
    draw_text_c(cx, cy + r + 44, wd[wday < 0 ? wday + 7 : wday], 0x8fa3c8, floor_c);
}

static void clock_enter(ui_app_t *self) {
    (void)self;
}

static void clock_event(ui_app_t *self, const input_event_t *e) {
    (void)self; (void)e;
}

/* -- Calculator -------------------------------------------------------- */

static int  c_acc, c_cur, c_entering;
static char c_op;

static int calc_apply(int a, int b, char op) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b ? a / b : 0;
    default:  return b;
    }
}

static void calc_reset(void) {
    c_acc = 0; c_cur = 0; c_entering = 0; c_op = 0;
}

static void calc_key(char k) {
    if (k >= '0' && k <= '9') {
        if (c_entering != 1) { c_cur = 0; c_entering = 1; }
        c_cur = c_cur * 10 + (k - '0');
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (c_entering == 1 && c_op && c_op != '=')
            c_acc = calc_apply(c_acc, c_cur, c_op);
        else if (c_entering == 1)
            c_acc = c_cur;
        c_op = k;
        c_entering = 0;
    } else if (k == '=' || k == '\n' || k == '\r') {
        if (c_op && c_op != '=') {
            c_cur = calc_apply(c_acc, c_cur, c_op);
            c_acc = c_cur;
            c_op = '=';
            c_entering = 2;   /* 2 = showing a result */
            kprintf("calc: result %d\n", c_cur);
        }
    } else if (k == 'c' || k == 'C') {
        calc_reset();
    }
}

static void calc_draw(ui_app_t *self, int x, int y, int w, int h) {
    (void)self;
    uint32_t bg = 0x10151f, panel = 0x1b2433, accent = 0x2a6df4;
    fb_fillrect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, bg);

    /* display */
    display_rounded_rect((uint32_t)x + 40, (uint32_t)y + 24, (uint32_t)(w - 80), 90,
                         panel);
    char disp[20];
    if (c_entering == 2) { itoa10(disp, c_acc); }
    else if (c_entering) itoa10(disp, c_cur);
    else                 itoa10(disp, c_op ? c_acc : c_cur);
    int tw = fb_text_width(disp);
    fb_drawstr_px((uint32_t)(x + w - 60 - tw), (uint32_t)(y + 60), disp,
                  0xffffff, panel);

    /* keypad */
    static const char *keys[5][4] = {
        { "C", "CE", "%", "/" }, { "7", "8", "9", "*" }, { "4", "5", "6", "-" },
        { "1", "2", "3", "+" }, { "0", ".", "=", "" },
    };
    int kw = (w - 80) / 4;
    int kh = (h - 150) / 5;
    int x0 = 40, y0 = STATUS_H + 132;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            const char *lbl = keys[r][c];
            if (!lbl[0]) continue;
            int kx = x0 + c * kw, ky = y0 + r * kh;
            uint32_t col = panel;
            if (c == 3 || (r == 0)) col = accent;
            else if (r == 4 && c == 2) col = 0x2f9e44;
            display_rounded_rect((uint32_t)(kx + 6), (uint32_t)(ky + 6),
                                 (uint32_t)(kw - 12), (uint32_t)(kh - 12), col);
            draw_text_c(kx + kw / 2, ky + kh / 2 - 8, lbl, 0xffffff, col);
        }
    }
    /* print geometry once, from enter(), for the harness */
}

static void calc_enter(ui_app_t *self) {
    (void)self;
    calc_reset();
}

static void calc_event(ui_app_t *self, const input_event_t *e) {
    (void)self;
    if (e->type != IN_KEY) return;
    kprintf("calc: key '%c'\n", (char)e->key);
    calc_key((char)e->key);
    g_dirty = 1;
}

/* -- Settings ---------------------------------------------------------- */

static const char *setting_names[4] = { "Wi-Fi", "Bluetooth", "Airplane Mode",
                                        "Do Not Disturb" };
static int setting_on[4] = { 1, 0, 0, 0 };

static void settings_draw(ui_app_t *self, int x, int y, int w, int h) {
    (void)self;
    uint32_t bg = 0x0f1720, panel = 0x1c2836;
    fb_fillrect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, bg);
    draw_text_c(x + w / 2, y + 16, "Settings", 0xffffff, bg);
    int ry = y + 60;
    for (int i = 0; i < 4; i++) {
        display_rounded_rect((uint32_t)x + 60, (uint32_t)ry,
                             (uint32_t)(w - 120), 64, panel);
        fb_drawstr_px((uint32_t)x + 84, (uint32_t)(ry + 24), setting_names[i],
                      0xe8edf5, panel);
        /* toggle */
        uint32_t tx = (uint32_t)(x + w - 60 - 84);
        uint32_t track = setting_on[i] ? 0x2f9e44 : 0x3a4657;
        display_rounded_rect(tx, (uint32_t)(ry + 18), 64, 28, track);
        uint32_t knob = setting_on[i] ? tx + 36 : tx + 4;
        display_rounded_rect(knob, (uint32_t)(ry + 22), 20, 20, 0xffffff);
        ry += 80;
    }
}

static void settings_event(ui_app_t *self, const input_event_t *e) {
    (void)self;
    if (e->type != IN_TOUCH_UP) return;
    int w = (int)fb_getwidth();
    int ry = STATUS_H + 60;
    for (int i = 0; i < 4; i++) {
        if (e->x >= 60 && e->x < w - 60 && e->y >= ry && e->y < ry + 64) {
            setting_on[i] = !setting_on[i];
            kprintf("ui: setting '%s'=%d\n", setting_names[i], setting_on[i]);
            g_dirty = 1;
            return;
        }
        ry += 80;
    }
}

/* -- Photos ------------------------------------------------------------ */

static int photo_sel = -1;
static const uint32_t photo_colors[9] = {
    0xc2410c, 0x15803d, 0x1d4ed8, 0x7e22ce, 0x0891b2,
    0xb91c1c, 0xa16207, 0x4d7c0f, 0xbe185d,
};

static void photos_draw(ui_app_t *self, int x, int y, int w, int h) {
    (void)self;
    uint32_t bg = 0x0d1117;
    fb_fillrect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, bg);
    draw_text_c(x + w / 2, y + 16, "Photos", 0xffffff, bg);
    int cols = 3, tile = 150, gap = 24;
    int total = cols * tile + (cols - 1) * gap;
    int x0 = (w - total) / 2, y0 = STATUS_H + 70;
    for (int i = 0; i < 9; i++) {
        int r = i / cols, c = i % cols;
        int tx = x0 + c * (tile + gap), ty = y0 + r * (tile + gap);
        display_rounded_rect((uint32_t)tx, (uint32_t)ty, (uint32_t)tile,
                             (uint32_t)tile, photo_colors[i]);
        char lbl[4];
        lbl[0] = (char)('1' + i); lbl[1] = 0;
        draw_text_c(tx + tile / 2, ty + tile / 2 - 8, lbl, 0xffffff,
                    photo_colors[i]);
        if (i == photo_sel)
            fb_drawrect((uint32_t)(tx - 4), (uint32_t)(ty - 4), tile + 8, tile + 8,
                        0xffffff);
    }
}

static void photos_event(ui_app_t *self, const input_event_t *e) {
    (void)self;
    if (e->type != IN_TOUCH_UP) return;
    int w = (int)fb_getwidth();
    int cols = 3, tile = 150, gap = 24;
    int total = cols * tile + (cols - 1) * gap;
    int x0 = (w - total) / 2;
    int y0 = STATUS_H + 70;
    for (int i = 0; i < 9; i++) {
        int r = i / cols, c = i % cols;
        int tx = x0 + c * (tile + gap), ty = y0 + r * (tile + gap);
        if (e->x >= tx && e->x < tx + tile && e->y >= ty && e->y < ty + tile) {
            photo_sel = i;
            kprintf("ui: photo selected %d\n", i + 1);
            g_dirty = 1;
            return;
        }
    }
}

/* ────────────────────────────────────────────────────────────────────────
 * Registry
 * ──────────────────────────────────────────────────────────────────────── */

static ui_app_t app_clock = {
    "Clock", 0x334155, ICON_CLOCK, clock_enter, clock_app_draw, clock_event, 0
};
static ui_app_t app_calc = {
    "Calculator", 0x1f6feb, ICON_CALC, calc_enter, calc_draw, calc_event, 0
};
static ui_app_t app_settings = {
    "Settings", 0x475569, ICON_GEAR, 0, settings_draw, settings_event, 0
};
static ui_app_t app_photos = {
    "Photos", 0x0f766e, ICON_PHOTO, 0, photos_draw, photos_event, 0
};

/* ────────────────────────────────────────────────────────────────────────
 * Navigation + event dispatch
 * ──────────────────────────────────────────────────────────────────────── */

void ui_launch(ui_app_t *app) {
    g_active = app;
    if (app->enter) app->enter(app);
    kprintf("ui: launch %s\n", app->name);
    g_dirty = 1;
}

void ui_home(void) {
    if (!g_active) return;
    g_active = 0;
    kprintf("ui: home\n");
    g_dirty = 1;
}

static void handle_key(int k) {
    if (k == IN_KEY_BACK || k == 27) {
        if (g_active) { ui_home(); return; }
    }
    if (g_active) {
        input_event_t e = { .type = IN_KEY, .x = 0, .y = 0, .key = k };
        if (g_active->event) g_active->event(g_active, &e);
        return;
    }
    if (k == IN_KEY_LEFT)       g_focus = (g_focus - 1 + g_count) % g_count;
    else if (k == IN_KEY_RIGHT) g_focus = (g_focus + 1) % g_count;
    else if (k == IN_KEY_UP)    g_focus = (g_focus - HOME_COLS + g_count) % g_count;
    else if (k == IN_KEY_DOWN)  g_focus = (g_focus + HOME_COLS) % g_count;
    else if (k == IN_KEY_ENTER || k == '\n' || k == '\r') {
        ui_launch(g_apps[g_focus]);
        return;
    } else return;
    g_dirty = 1;
}

static void dispatch(const input_event_t *e) {
    if (e->type == IN_KEY) { handle_key(e->key); return; }

    g_px = e->x; g_py = e->y;
    if (g_active) {
        if (hit_back(e->x, e->y)) {
            if (e->type == IN_TOUCH_UP) ui_home();
            g_dirty = 1;
            return;
        }
        if (g_active->event) g_active->event(g_active, e);
        return;
    }

    int idx = home_hit(e->x, e->y);
    if (e->type == IN_TOUCH_DOWN) {
        if (idx >= 0) g_focus = idx;
    } else if (e->type == IN_TOUCH_UP) {
        if (idx >= 0 && idx == g_focus) ui_launch(g_apps[idx]);
    }
    g_dirty = 1;
}

/* ────────────────────────────────────────────────────────────────────────
 * Main loop
 * ──────────────────────────────────────────────────────────────────────── */

static void render(void) {
    if (g_active && g_active->draw)
        g_active->draw(g_active, 0, STATUS_H,
                       (int)fb_getwidth(), (int)fb_getheight() - STATUS_H);
    else
        draw_home();
    draw_status();
    /* draw_pointer() would go here */
}

void ui_init(void) {
    g_count = 0;
    ui_register(&app_clock);
    ui_register(&app_calc);
    ui_register(&app_settings);
    ui_register(&app_photos);

    for (int i = 0; i < g_count; i++) {
        int x, y;
        home_cell(i, &x, &y);
        kprintf("ui: app %d '%s' center=%d,%d r=%d\n",
                i, g_apps[i]->name, x + HOME_ICON / 2, y + HOME_ICON / 2,
                HOME_ICON / 2);
    }
    kprintf("ui: ready (%d apps)\n", g_count);
}

void ui_run(void) {
    uint64_t last = rtc_read_wallclock();
    render();
    for (;;) {
        int did = 0;
        input_event_t e;
        while (input_poll(&e)) { dispatch(&e); did = 1; }

        uint64_t now = rtc_read_wallclock();
        if (now != last) {
            last = now;
            if (g_active && g_active->tick) g_active->tick(g_active);
            did = 1;
        }
        if (did || g_dirty) {
            render();
            g_dirty = 0;
        }
        for (volatile int spin = 0; spin < 20000; spin++) { }
    }
}
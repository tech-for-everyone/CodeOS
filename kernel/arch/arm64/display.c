/* Display driver for the QEMU virt ARM64 platform.
 *
 * Uses a ramfb device (-device ramfb): the guest publishes a framebuffer
 * descriptor (RamFBCfg) through the fw_cfg "etc/ramfb" entry using DMA, and
 * QEMU renders that guest-RAM buffer as the display.
 *
 * Then it paints a mobile-style home screen: gradient wallpaper, status bar
 * with live clock (PL031), and an app grid.
 */
#include "display.h"
#include "fw_cfg.h"
#include "fb.h"
#include "rtc.h"
#include "../kernel/string.h"
#include "kprintf.h"

#define SCREEN_W     1024
#define SCREEN_H     600
#define PITCH        (SCREEN_W * 4)
#define FRAMEBUFFER_PHYS 0x41000000UL

#define DRM_FORMAT_XRGB8888 0x34325258UL

typedef struct {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} __attribute__((packed)) RamFBCfg;

static uint16_t ramfb_sel;

static inline uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static inline uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }

static void row_fill(uint32_t y, uint32_t color) {
    volatile uint32_t *p = (volatile uint32_t *)(FRAMEBUFFER_PHYS + (uint64_t)y * PITCH);
    for (uint32_t x = 0; x < SCREEN_W; x++) p[x] = color;
}

int display_init(void) {
    uint32_t size = 0;
    if (!fw_cfg_find_file("etc/ramfb", &ramfb_sel, &size)) {
        kprintf("display: etc/ramfb not found; add '-device ramfb' to QEMU\n");
        return 0;
    }
    kprintf("display: etc/ramfb @ sel %u (size %u)\n", ramfb_sel, size);

    static RamFBCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.addr   = bswap64(FRAMEBUFFER_PHYS);
    cfg.fourcc = bswap32(DRM_FORMAT_XRGB8888);
    cfg.width  = bswap32(SCREEN_W);
    cfg.height = bswap32(SCREEN_H);
    cfg.stride = bswap32(PITCH);

    if (!fw_cfg_dma_write(ramfb_sel, &cfg, sizeof(cfg))) {
        kprintf("display: failed to configure ramfb\n");
        return 0;
    }

    if (!fb_init(FRAMEBUFFER_PHYS, SCREEN_W, SCREEN_H, PITCH, 32, DRM_FORMAT_XRGB8888)) {
        kprintf("display: fb_init failed\n");
        return 0;
    }

    kprintf("display: framebuffer %ux%u @ 0x%016lx\n", SCREEN_W, SCREEN_H,
            (uint64_t)FRAMEBUFFER_PHYS);
    return 1;
}

/* ------------------------------------------------------------------ */
/* small drawing helpers                                               */
/* ------------------------------------------------------------------ */

static void circle_outline(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        fb_putpixel(cx + x, cy + y, color); fb_putpixel(cx - x, cy + y, color);
        fb_putpixel(cx + x, cy - y, color); fb_putpixel(cx - x, cy - y, color);
        fb_putpixel(cx + y, cy + x, color); fb_putpixel(cx - y, cy + x, color);
        fb_putpixel(cx + y, cy - x, color); fb_putpixel(cx - y, cy - x, color);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static void rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t color) {
    uint32_t r = 14;
    fb_fillrect(x + r, y, w - 2 * r, h, color);
    fb_fillrect(x, y + r, w, h - 2 * r, color);
    fb_fillrect(x + r / 2, y + r / 2, r, r, color);
    fb_fillrect(x + w - r - r / 2, y + r / 2, r, r, color);
    fb_fillrect(x + r / 2, y + h - r - r / 2, r, r, color);
    fb_fillrect(x + w - r - r / 2, y + h - r - r / 2, r, r, color);
}

/* ------------------------------------------------------------------ */
/* app icons (drawn with primitives only)                              */
/* ------------------------------------------------------------------ */

enum {
    ICON_TERM, ICON_FOLDER, ICON_GLOBE, ICON_MAIL,
    ICON_CHAT, ICON_PHOTO, ICON_MUSIC, ICON_MAP,
    ICON_CLOCK, ICON_GEAR, ICON_INFO, ICON_BELL,
};

static void draw_icon(int type, uint32_t cx, uint32_t cy, uint32_t r, uint32_t color) {
    switch (type) {
    case ICON_TERM: /* >_ */
        fb_fillrect(cx - r * 3 / 5, cy - r / 2, r / 5, r, color);
        fb_fillrect(cx - r / 2, cy + r / 3, r, r / 5, color);
        break;
    case ICON_FOLDER: /* folder with tab */
        fb_fillrect(cx - r * 3 / 4, cy - r / 4, r * 3 / 4, r / 3, color);
        fb_fillrect(cx - r * 3 / 4, cy - r / 4 + r / 3, r * 3 / 2, r * 3 / 4, color);
        break;
    case ICON_GLOBE: /* globe = circle + meridian arcs */
        circle_outline(cx, cy, (int)(r * 2 / 3), color);
        fb_drawline(cx, cy - (int)(r * 2 / 3), cx, cy + (int)(r * 2 / 3), color);
        fb_drawline(cx - (int)(r * 2 / 3), cy, cx + (int)(r * 2 / 3), cy, color);
        fb_drawline(cx - (int)(r * 2 / 3), cy, cx + (int)(r * 2 / 3), cy, color);
        break;
    case ICON_MAIL: /* envelope */
        fb_drawrect(cx - r * 3 / 4, cy - r / 2, r * 3 / 2, r, color);
        fb_drawline(cx - r * 3 / 4, cy - r / 2, cx, cy, color);
        fb_drawline(cx + r * 3 / 4, cy - r / 2, cx, cy, color);
        break;
    case ICON_CHAT: /* chat bubble */
        fb_drawrect(cx - r * 3 / 4, cy - r / 2, r * 3 / 2, r, color);
        fb_drawline(cx - r / 2, cy + r / 2, cx - r / 2, cy + r, color);
        fb_drawline(cx - r / 2, cy + r, cx, cy + r / 2, color);
        break;
    case ICON_PHOTO: /* mountain + sun */
        fb_drawline(cx - r * 3 / 4, cy + r / 2, cx - r / 4, cy - r / 4, color);
        fb_drawline(cx - r / 4, cy - r / 4, cx + r / 4, cy + r / 4, color);
        fb_drawline(cx + r / 4, cy + r / 4, cx + r * 3 / 4, cy - r / 4, color);
        fb_drawline(cx + r * 3 / 4, cy - r / 4, cx + r * 3 / 4 + 1, cy + r / 2, color);
        circle_outline(cx + r * 3 / 8, cy - r / 2, r / 5, color);
        break;
    case ICON_MUSIC: /* music note */
        fb_drawrect(cx + r / 2, cy - r * 3 / 4, r / 6, r, color);
        circle_outline(cx - r / 4, cy + r / 3, r / 4, color);
        circle_outline(cx + r / 2, cy + r / 3, r / 4, color);
        fb_drawline(cx + r / 2 + r / 4, cy + r / 3, cx + r / 2 + r / 4, cy - r * 3 / 4, color);
        break;
    case ICON_MAP: /* map pin */
        circle_outline(cx, cy - r / 4, (int)(r / 2), color);
        fb_fillrect(cx - 1, cy, 3, r / 3, color);
        fb_drawline(cx, cy + r / 3, cx - r / 3, cy + r / 2, color);
        fb_drawline(cx, cy + r / 3, cx + r / 3, cy + r / 2, color);
        fb_drawline(cx - r / 3, cy + r / 2, cx + r / 3, cy + r / 2, color);
        break;
    case ICON_CLOCK: /* clock face */
        circle_outline(cx, cy, (int)(r * 2 / 3), color);
        fb_fillrect(cx - 1, cy, 3, r / 4, color);
        fb_fillrect(cx - 1, cy - r / 4, 3, r / 4, color);
        fb_fillrect(cx, cy - 1, r / 4, 3, color);
        fb_fillrect(cx - r / 4, cy - 1, r / 4, 3, color);
        break;
    case ICON_GEAR: /* gear: circle + teeth */
        circle_outline(cx, cy, (int)(r / 3), color);
        for (int i = 0; i < 8; i++) {
            int a = i * 45;
            int dx = (a == 90 || a == 270) ? 0 : 1;
            int dy = (a == 0 || a == 180) ? 0 : 1;
            fb_fillrect(cx + (int)((r * 2 / 3) * (a == 0 ? 1 : a == 180 ? -1 : 0)) - (dx ? r / 8 : 0),
                        cy + (int)((r * 2 / 3) * (a == 90 ? 1 : a == 270 ? -1 : 0)) - (dy ? r / 8 : 0),
                        dx ? r / 4 : r / 6, dy ? r / 4 : r / 6, color);
        }
        break;
    case ICON_INFO: /* i in a circle */
        circle_outline(cx, cy, (int)(r * 2 / 3), color);
        fb_fillrect(cx - 2, cy - r / 4, 5, 3, color);
        fb_fillrect(cx - 2, cy + r / 4 - 6, 5, r / 4, color);
        break;
    case ICON_BELL: /* bell */
        circle_outline(cx, cy - r / 3, (int)(r / 2), color);
        fb_drawline(cx - r / 2, cy - r / 3 + r / 2, cx - r / 2, cy + r / 3, color);
        fb_drawline(cx + r / 2, cy - r / 3 + r / 2, cx + r / 2, cy + r / 3, color);
        fb_drawline(cx - r / 2, cy + r / 3, cx + r / 2, cy + r / 3, color);
        fb_fillrect(cx - r / 3, cy + r / 2, r * 2 / 3, r / 5, color);
        break;
    default:
        break;
    }
}

static void draw_status_bar(uint32_t clock) {
    /* bar background */
    fb_fillrect(0, 0, SCREEN_W, 36, 0x0f1522);
    fb_fillrect(0, 36, SCREEN_W, 2, 0x2a3a5c);

    fb_drawstr_px(16, 10, "Zircon 1.0", 0xdfe6f2, 0x0f1522);

    /* network bars */
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 4;
        fb_fillrect(SCREEN_W - 96 - i * 8, 28 - h, 5, h, 0x8fa3c8);
    }
    /* battery */
    fb_drawrect(SCREEN_W - 56, 12, 24, 12, 0x8fa3c8);
    fb_fillrect(SCREEN_W - 30, 15, 3, 6, 0x8fa3c8);
    fb_fillrect(SCREEN_W - 54, 14, 18, 8, 0x4caf50);

    /* clock */
    char buf[16];
    uint32_t sod = clock % 86400;
    uint32_t h = sod / 3600, m = (sod % 3600) / 60;
    buf[0] = (char)('0' + h / 10); buf[1] = (char)('0' + h % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + m / 10); buf[4] = (char)('0' + m % 10);
    buf[5] = 0;
    fb_drawstr_px(SCREEN_W - 104, 10, buf, 0xffffff, 0x0f1522);

    /* date */
    uint32_t days = clock / 86400;   /* days since 1970 */
    uint32_t wday = (days + 4) % 7;  /* 1970-01-01 was Thursday */
    const char *names[7] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
    fb_drawstr_px(SCREEN_W - 196, 10, names[wday], 0x8fa3c8, 0x0f1522);
}

/* ------------------------------------------------------------------ */
/* home screen                                                         */
/* ------------------------------------------------------------------ */

static void draw_wallpaper(void) {
    /* vertical gradient: deep blue -> navy */
    for (uint32_t y = 0; y < SCREEN_H; y++) {
        uint32_t t = (y * 200) / SCREEN_H;
        uint8_t r = 0x33 + (uint8_t)((0x0b - 0x33) * t / 200);
        uint8_t g = 0x47 + (uint8_t)((0x1d - 0x47) * t / 200);
        uint8_t b = 0x66 + (uint8_t)((0x33 - 0x66) * t / 200);
        row_fill(y, FB_RGB(r, g, b));
    }
    /* a soft glow accent near the top */
    for (int i = 0; i < 3; i++)
        circle_outline(SCREEN_W / 2, SCREEN_H / 2 - 60, 160 + i * 40, 0x1c2b4a);
}

static void draw_app_grid(void) {
    typedef struct { const char *name; uint32_t color; int icon; } AppDef;
    static const AppDef apps[] = {
        { "Terminal",  0x2b2f3a, ICON_TERM   },
        { "Files",     0x1f6feb, ICON_FOLDER },
        { "Browser",   0x1282a2, ICON_GLOBE  },
        { "Mail",      0x0a7d60, ICON_MAIL   },
        { "Messages",  0x0f8b8d, ICON_CHAT   },
        { "Photos",    0x0f766e, ICON_PHOTO  },
        { "Music",     0x9333ea, ICON_MUSIC  },
        { "Maps",      0x16a34a, ICON_MAP    },
        { "Clock",     0x334155, ICON_CLOCK  },
        { "Settings",  0x475569, ICON_GEAR   },
        { "Info",      0x2563eb, ICON_INFO   },
        { "Notify",    0xb45309, ICON_BELL   },
    };
    const int n = (int)(sizeof(apps) / sizeof(apps[0]));
    const int cols = 4;
    const int icon = 88, gap = 48;
    int total_w = cols * icon + (cols - 1) * gap;
    int x0 = (SCREEN_W - total_w) / 2;
    int y0 = 150;
    for (int i = 0; i < n; i++) {
        int r = i / cols, c = i % cols;
        int x = x0 + c * (icon + gap);
        int y = y0 + r * (icon + 36);
        rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)icon, (uint32_t)icon, apps[i].color);
        draw_icon(apps[i].icon, x + icon / 2, y + icon / 2, icon / 2, 0xffffff);
        int tw = fb_text_width(apps[i].name);
        fb_drawstr_px((uint32_t)(x + (icon - tw) / 2), (uint32_t)(y + icon + 6),
                      apps[i].name, 0xe8edf5, 0x000000);
    }
}

void display_home_screen(void) {
    if (!fb_getwidth()) return;
    fb_setbg(0x0b1d33);
    fb_clear();
    draw_wallpaper();
    draw_app_grid();
    draw_status_bar(rtc_read_wallclock());
    fb_cursor_hide();
}

void display_update_clock(void) {
    if (!fb_getwidth()) return;
    draw_status_bar(rtc_read_wallclock());
}

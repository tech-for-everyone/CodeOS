#include "fb.h"
#include "font8x16.h"
#include "../kernel/string.h"
#include "kprintf.h"

#define MAX_WIDTH  1024
#define MAX_HEIGHT 768

static uint32_t fb_w, fb_h, fb_pitch, fb_bpp;
static uint64_t fb_addr;
static int fb_initialized;
static uint32_t cur_fg = 0xFFFFFF, cur_bg = 0x000000;
static int cursor_row, cursor_col;
static int cursor_visible;

void fb_drawpixel(uint32_t x, uint32_t y, uint32_t color);

int fb_init(uint64_t addr, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp, uint32_t type) {
    (void)type;
    if (!addr || w == 0 || h == 0) return 0;
    fb_addr = addr;
    fb_w = w;
    fb_h = h;
    fb_pitch = pitch;
    fb_bpp = bpp;
    fb_initialized = 1;
    fb_cursor_init();
    return 1;
}

uint32_t fb_getwidth(void) { return fb_w; }
uint32_t fb_getheight(void) { return fb_h; }
int fb_get_rows(void) { return fb_h ? fb_h / 16 : 0; }
int fb_get_cols(void) { return fb_w ? fb_w / 8 : 0; }

void fb_putchar(char c) {
    if (!fb_initialized) return;
    int cols = fb_get_cols(), rows = fb_get_rows();
    if (c == '\n') {
        cursor_row++;
        if (cursor_row >= rows) { fb_scroll(); cursor_row = rows - 1; }
        cursor_col = 0;
        return;
    }
    if (c == '\r') { cursor_col = 0; return; }
    if (c == '\b') {
        if (cursor_col > 0) cursor_col--;
        return;
    }
    if (c == '\t') {
        do { fb_drawchar_px(cursor_col * 8, cursor_row * 16, ' ', cur_fg, cur_bg);
             cursor_col++; } while (cursor_col < cols && (cursor_col % 4));
        return;
    }
    fb_drawchar_px(cursor_col * 8, cursor_row * 16, c, cur_fg, cur_bg);
    cursor_col++;
    if (cursor_col >= cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= rows) { fb_scroll(); cursor_row = rows - 1; }
    }
}

void fb_write(const char *s) {
    if (!s) return;
    while (*s) fb_putchar(*s++);
}

void fb_clear(void) {
    if (!fb_initialized) return;
    fb_fill(cur_bg);
    cursor_row = 0;
    cursor_col = 0;
}

void fb_scroll(void) {
    if (!fb_initialized) return;
    uint32_t row_bytes = fb_pitch * 16;
    uint32_t total = row_bytes * (fb_get_rows() - 1);
    volatile uint8_t *base = (volatile uint8_t *)fb_addr;
    for (uint32_t i = 0; i < total; i++)
        base[i] = base[i + row_bytes];
    /* clear last row */
    for (uint32_t r = 0; r < 16; r++)
        for (uint32_t x = 0; x < fb_w; x++)
            fb_drawpixel(x, (fb_get_rows() - 1) * 16 + r, cur_bg);
}

void fb_setfg(uint32_t color) { cur_fg = color; }
void fb_setbg(uint32_t color) { cur_bg = color; }

void fb_fill(uint32_t color) { fb_fillrect(0, 0, fb_w, fb_h, color); }

void fb_cursor_init(void) { cursor_row = 0; cursor_col = 0; cursor_visible = 1; }
void fb_cursor_show(void) { cursor_visible = 1; fb_cursor_render(); }
void fb_cursor_hide(void) { cursor_visible = 0; }
void fb_cursor_move(int x, int y) { cursor_col = x; cursor_row = y; }
void fb_cursor_render(void) {
    if (!fb_initialized || !cursor_visible) return;
    int x = cursor_col * 8, y = cursor_row * 16;
    for (int i = 0; i < 8; i++) {
        fb_drawpixel(x + i, y + 15, cur_fg);
        fb_drawpixel(x + i, y + 14, cur_fg);
    }
}

void fb_move_cursor(int row, int col) { cursor_row = row; cursor_col = col; }

void fb_write_at(int row, int col, const char *s) {
    fb_move_cursor(row, col);
    fb_write(s);
}

void fb_write_styled(int row, int col, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t ofg = cur_fg, obg = cur_bg;
    fb_setfg(fg);
    fb_setbg(bg);
    fb_write_at(row, col, s);
    fb_setfg(ofg);
    fb_setbg(obg);
}

void fb_fill_row(int row) { fb_fill_row_bg(row, cur_bg); }

void fb_fill_row_bg(int row, uint32_t bg) {
    if (!fb_initialized) return;
    for (uint32_t r = 0; r < 16; r++)
        for (uint32_t x = 0; x < fb_w; x++)
            fb_drawpixel(x, row * 16 + r, bg);
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_initialized || x >= fb_w || y >= fb_h) return;
    volatile uint32_t *p = (volatile uint32_t *)(fb_addr + y * fb_pitch + x * 4);
    *p = color;
}

/* Draw one 8x16 glyph at pixel position (x, y). */
void fb_drawchar_px(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (!fb_initialized || x >= fb_w || y >= fb_h) return;
    uint8_t ch = (uint8_t)c;
    const uint8_t *glyph = &font8x16[ch * 16];
    for (int row = 0; row < 16; row++) {
        if (y + row >= fb_h) break;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (x + col >= fb_w) break;
            fb_drawpixel(x + col, y + row, (bits & (0x80 >> col)) ? fg : bg);
        }
    }
}

void fb_drawchar(int x, int y, char c, uint32_t fg, uint32_t bg) {
    fb_drawchar_px((uint32_t)x, (uint32_t)y, c, fg, bg);
}

void fb_drawstr_px(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    if (!s) return;
    uint32_t cx = x;
    while (*s) {
        fb_drawchar_px(cx, y, *s++, fg, bg);
        cx += 8;
    }
}

void fb_drawstr_aa(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    fb_drawstr_px(x, y, s, fg, bg);
}

int fb_text_width(const char *s) { return s ? (int)strlen(s) * 8 : 0; }

void fb_drawpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_initialized || x >= fb_w || y >= fb_h) return;
    volatile uint32_t *p = (volatile uint32_t *)(fb_addr + y * fb_pitch + x * 4);
    *p = color;
}

void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = 0; row < h && y + row < fb_h; row++)
        for (uint32_t col = 0; col < w && x + col < fb_w; col++)
            fb_drawpixel(x + col, y + row, color);
}

void fb_drawrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fb_drawline(x, y, x + w - 1, y, color);
    fb_drawline(x, y + h - 1, x + w - 1, y + h - 1, color);
    fb_drawline(x, y, x, y + h - 1, color);
    fb_drawline(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void fb_drawline(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        fb_drawpixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

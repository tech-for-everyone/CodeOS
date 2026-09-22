#ifndef FB_H
#define FB_H

#include <stdint.h>

#define FB_RGB(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))

#define CURSOR_STYLE_COUNT 8

struct fb_info_t {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
};

extern struct fb_info_t fb;

int  fb_init(uint64_t addr_phys, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp, uint32_t type);
int  fb_available(void);
uint32_t fb_getwidth(void);
uint32_t fb_getheight(void);
int  fb_get_pitch(void);
uint8_t  fb_get_bpp(void);
uint64_t fb_get_addr_phys(void);
int  fb_get_rows(void);
int  fb_get_cols(void);

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill(uint32_t color);
void fb_drawchar(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_drawchar_px(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_drawstr_px(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
void fb_drawstr_aa(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
int  fb_text_width(const char *s);
void fb_putchar(char c);
void fb_write(const char *s);
void fb_clear(void);
void fb_scroll(void);
void fb_setfg(uint32_t color);
void fb_setbg(uint32_t color);
void fb_move_cursor(int row, int col);
void fb_write_at(int row, int col, const char *s);
void fb_write_styled(int row, int col, const char *s, uint32_t fg, uint32_t bg);
void fb_fill_row(int row);
void fb_fill_row_bg(int row, uint32_t bg);

void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_drawrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_drawline(int x0, int y0, int x1, int y1, uint32_t color);

void fb_cursor_init(void);
void fb_cursor_move(int x, int y);
void fb_cursor_render(void);
void fb_cursor_hide(void);
void fb_cursor_show(void);
void fb_cursor_invalidate(void);
void fb_cursor_set_style(int style);
int  fb_cursor_get_style(void);

int fb_backbuffer_init(void);
void fb_backbuffer_begin(void);
void fb_backbuffer_end(void);
uint32_t *fb_get_active_buffer(void);

void xs_init(void);
void xs_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void xs_drawrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, int thickness);
void xs_drawtext(int x, int y, const char *text, uint32_t color, int size);
void xs_present(void);

#endif

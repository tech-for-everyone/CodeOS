#ifndef FB_H
#define FB_H

#include "types.h"

#define FB_RGB(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))

int fb_init(uint64_t addr, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp, uint32_t type);
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
uint32_t fb_getwidth(void);
uint32_t fb_getheight(void);
int  fb_get_rows(void);
int  fb_get_cols(void);
void fb_move_cursor(int row, int col);
void fb_write_at(int row, int col, const char *s);
void fb_write_styled(int row, int col, const char *s, uint32_t fg, uint32_t bg);
void fb_fill_row(int row);
void fb_fill_row_bg(int row, uint32_t bg);

void fb_cursor_init(void);
void fb_cursor_move(int x, int y);
void fb_cursor_render(void);
void fb_cursor_hide(void);
void fb_cursor_show(void);

void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_drawrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_drawline(int x0, int y0, int x1, int y1, uint32_t color);

#endif

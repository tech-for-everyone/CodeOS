#ifndef LAUNCHER_HEADER_H
#define LAUNCHER_HEADER_H

#include <stdint.h>

#define LAUNCHER_SEARCH_MAX 32

int  launcher_init(void);
void launcher_draw(uint32_t scr_w, uint32_t scr_h);
int  launcher_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  launcher_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  launcher_is_open(void);
void launcher_toggle(void);
void launcher_close(void);
int  launcher_key(int key);
int  launcher_update(uint64_t now_ms);

#endif

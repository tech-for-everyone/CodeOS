#ifndef DOCK_HEADER_H
#define DOCK_HEADER_H

#include <stdint.h>

#define DOCK_H      80
#define DOCK_ICON_SZ 44

int  dock_init(void);
void dock_draw(uint32_t scr_w, uint32_t scr_h);
int  dock_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  dock_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  dock_h(void);
void dock_tick(int mx);

#endif

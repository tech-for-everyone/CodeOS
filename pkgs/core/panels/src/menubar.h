#ifndef MENUBAR_HEADER_H
#define MENUBAR_HEADER_H

#include <stdint.h>

#define MENUBAR_H 24

void menubar_init(void);
void menubar_draw(uint32_t scr_w, uint32_t scr_h, int active_win, const char *active_title);
int  menubar_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  menubar_should_toggle_launcher(void);
int  menubar_h(void);
int  menubar_apple_menu_click(int mx, int my);
void menubar_draw_apple_menu(uint32_t scr_w);

#endif

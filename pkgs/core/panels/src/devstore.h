#ifndef DEVSTORE_H
#define DEVSTORE_H

#include <stdint.h>

void devstore_open(void);
int  devstore_is_open(void);
void devstore_draw(uint32_t scr_w, uint32_t scr_h);
int  devstore_click(int mx, int my);
int  devstore_key(int key);
void devstore_mouse_move(int mx, int my);

#endif

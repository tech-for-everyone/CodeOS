#ifndef LAUNCHPAD_H
#define LAUNCHPAD_H

#include <stdint.h>

int  launchpad_init(void);
int  launchpad_is_open(void);
void launchpad_toggle(void);
void launchpad_close(void);
void launchpad_draw(uint32_t scr_w, uint32_t scr_h);
int  launchpad_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  launchpad_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  launchpad_key(int key);
int  launchpad_update(uint64_t now_ms);

#endif

#ifndef SWITCHER_H
#define SWITCHER_H

#include <stdint.h>

void switcher_init(void);
void switcher_activate(void);
void switcher_deactivate(void);
void switcher_cancel(void);
int  switcher_active(void);
void switcher_next(void);
void switcher_prev(void);
void switcher_draw(void);
int  switcher_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);

#endif

#ifndef ZIRCON_H
#define ZIRCON_H

#include "types.h"

/* Zircon Debug Mode */
void zdm_toggle(void);
int  zdm_active(void);
void zdm_draw(void);

void zircon_init(void);
void zircon_draw(void);
int  zircon_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
void zircon_notify(const char *title, const char *text);
void zircon_toggle_drawer(void);
void zircon_toggle_qs(void);
void zircon_toggle_shade(void);
int  zircon_shade_open(void);
int  zircon_key(int key);

#endif

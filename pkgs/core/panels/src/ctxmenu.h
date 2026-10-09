#ifndef CTXMENU_H
#define CTXMENU_H

#include <stdint.h>

#define CTXMENU_MAX_ITEMS 12
#define CTXMENU_ITEM_H    24
#define CTXMENU_W         180

typedef void (*ctxmenu_action_t)(void);

typedef struct {
    const char *label;
    ctxmenu_action_t action;
    int separator;
} ctxmenu_item_t;

void ctxmenu_init(void);
void ctxmenu_open(int x, int y, const ctxmenu_item_t *items, int count);
void ctxmenu_close(void);
int  ctxmenu_is_open(void);
void ctxmenu_draw(uint32_t scr_w, uint32_t scr_h);
int  ctxmenu_click(int mx, int my);
void ctxmenu_mousemove(int mx, int my);

#endif

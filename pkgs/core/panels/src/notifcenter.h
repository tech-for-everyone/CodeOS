#ifndef NOTIFCENTER_H
#define NOTIFCENTER_H

#include <stdint.h>

#define NOTIF_MAX 8
#define NOTIF_TEXT_MAX 64

typedef struct {
    char text[NOTIF_TEXT_MAX];
    uint32_t icon_color;
    int dismiss;
} notif_entry_t;

void notifcenter_init(void);
void notifcenter_toggle(void);
int  notifcenter_is_open(void);
void notifcenter_add(const char *text, bool unread);
void notifcenter_draw(uint32_t scr_w, uint32_t scr_h);
int  notifcenter_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
void notifcenter_tick(void);
void notifcenter_mousemove(int mx, int my, uint32_t scr_w, uint32_t scr_h);

#endif

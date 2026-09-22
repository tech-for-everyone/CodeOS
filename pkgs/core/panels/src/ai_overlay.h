#ifndef AI_OVERLAY_H
#define AI_OVERLAY_H

void ai_overlay_init(void);
int  ai_overlay_is_open(void);
void ai_overlay_toggle(void);
void ai_overlay_draw(int scr_w, int scr_h);
int  ai_overlay_click(int mx, int my);
void ai_overlay_key(int key);
void ai_overlay_mousemove(int mx, int my);

#endif

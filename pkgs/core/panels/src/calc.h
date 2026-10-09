#ifndef CALC_APP_H
#define CALC_APP_H

void calc_open(void);
int  calc_is_open(void);
void calc_close(void);
void calc_draw(uint32_t scr_w, uint32_t scr_h);
int  calc_click(int mx, int my);
void calc_mousemove(int mx, int my);
void calc_key(int key);

#endif

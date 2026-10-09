#ifndef TERMINAL_H
#define TERMINAL_H

int  terminal_open(void);
int  terminal_is_open(void);
int  terminal_is_focused(void);
void terminal_draw(uint32_t scr_w, uint32_t scr_h);
void terminal_key(int key);
void terminal_close(void);
int  terminal_win_idx(void);

#endif

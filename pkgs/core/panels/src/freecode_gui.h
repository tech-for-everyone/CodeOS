#ifndef FREECODE_GUI_H
#define FREECODE_GUI_H

void freecode_gui_open(void);
void freecode_gui_close(void);
int  freecode_gui_is_open(void);
void freecode_gui_draw(void);
int  freecode_gui_click(int mx, int my);
void freecode_gui_mousemove(int mx, int my);
void freecode_gui_key(int key);

#endif

#ifndef ZIGGY_GUI_H
#define ZIGGY_GUI_H

void ziggy_gui_open(void);
void ziggy_gui_close(void);
int  ziggy_gui_is_open(void);
void ziggy_gui_draw(void);
int  ziggy_gui_click(int mx, int my);
void ziggy_gui_mousemove(int mx, int my);
void ziggy_gui_key(int key);

#endif

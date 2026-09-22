#ifndef SETTINGS_H
#define SETTINGS_H

int  settings_open(void);
int  settings_is_open(void);
void settings_draw(void);
int  settings_click(int mx, int my);
void settings_close(void);
void settings_mousemove(int mx, int my);

#endif

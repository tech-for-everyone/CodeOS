#ifndef FMANAGER_H
#define FMANAGER_H

int fmanager_open(void);
int fmanager_is_open(void);
void fmanager_close(void);
void fmanager_draw(void);
int fmanager_click(int mx, int my);
int fmanager_mousemove(int mx, int my);
int fmanager_mouseup(void);
void fmanager_key(int key);

#endif

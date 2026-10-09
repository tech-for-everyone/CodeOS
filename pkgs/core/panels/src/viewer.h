#ifndef _VIEWER_H
#define _VIEWER_H

int  viewer_open(void);
int  viewer_open_with(const char *name);
int  viewer_is_open(void);
void viewer_close(void);
void viewer_draw(void);
int  viewer_click(int mx, int my);

#endif

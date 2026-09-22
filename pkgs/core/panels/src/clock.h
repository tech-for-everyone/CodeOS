#ifndef CLOCK_HEADER_H
#define CLOCK_HEADER_H

void clock_open(void);
int  clock_is_open(void);
void clock_close(void);
void clock_draw(void);
int  clock_click(int mx, int my);

#endif
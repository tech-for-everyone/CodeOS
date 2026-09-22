#ifndef SYSMON_H
#define SYSMON_H

void sysmon_open(void);
int  sysmon_is_open(void);
void sysmon_close(void);
void sysmon_draw(void);
int  sysmon_click(int mx, int my);
void sysmon_key(int key);

#endif

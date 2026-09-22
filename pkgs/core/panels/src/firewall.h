#ifndef FIREWALL_H
#define FIREWALL_H

void firewall_open(void);
int  firewall_is_open(void);
void firewall_close(void);
void firewall_draw(uint32_t scr_w, uint32_t scr_h);
int  firewall_click(int mx, int my);
void firewall_key(int key);

#endif

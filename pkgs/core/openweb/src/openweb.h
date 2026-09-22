#ifndef OPENWEB_H
#define OPENWEB_H

#define OW_URL_MAX     512
#define OW_CONTENT_MAX 65535
#define OW_MAX_TABS    12

int  openweb_open(void);
int  openweb_is_open(void);
void openweb_draw(void);
int  openweb_click(int mx, int my);
void openweb_mousemove(int mx, int my);
int  openweb_key(int key);
void openweb_close(void);

#endif

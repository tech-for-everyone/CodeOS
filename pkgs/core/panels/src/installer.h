#ifndef INSTALLER_H
#define INSTALLER_H

int  installer_open(void);
int  installer_is_open(void);
void installer_close(void);
void installer_draw(void);
void installer_key(int key);

#endif

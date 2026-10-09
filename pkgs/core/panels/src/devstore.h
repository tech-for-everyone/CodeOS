#ifndef DEVSTORE_H
#define DEVSTORE_H

#include <stdint.h>

/* devstore_click() return value that means "the Install App button", as
 * opposed to an entry index. Chosen to be past any plausible entry count so
 * it cannot collide with a real index. */
#define DS_INSTALL_HIT  (-2)
#define DS_MISS         (-1)

/* Lifecycle. devstore_open() re-scans the filesystem and sets the open flag;
 * devstore_close() clears the flag and drops the listing. The open flag is
 * independent of the entry count, so an empty store still reports open. */
void devstore_open(void);
void devstore_close(void);
int  devstore_is_open(void);

/* Re-scan without changing the open state. */
void devstore_refresh(void);

/* Drawing. scr_w/scr_h are accepted for signature compatibility with the
 * other panels in this directory; DevStore uses a fixed 280x280 geometry. */
void devstore_draw(uint32_t scr_w, uint32_t scr_h);

/* Input. devstore_key() returns 1 if the key was consumed. */
int  devstore_click(int mx, int my);
int  devstore_key(int key);
void devstore_mouse_move(int mx, int my);

/* Entry accessors. devstore_get_entry_count() returns how many entries the
 * last scan found; the getters return 1 on success and 0 for an out-of-range
 * index, a NULL buffer or a zero buffer size. */
int  devstore_get_entry_count(void);
int  devstore_get_entry_name(int idx, char *buf, int buf_sz);
int  devstore_get_entry_path(int idx, char *buf, int buf_sz);

#endif

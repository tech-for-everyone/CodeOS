#ifndef _BOOTSPLASH_H
#define _BOOTSPLASH_H

void bootsplash_init(void);
void bootsplash_tick(void);
void bootsplash_set_progress(int percent, const char *msg);
void bootsplash_set_message(const char *msg);
void bootsplash_finish(void);

#endif
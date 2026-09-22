#ifndef UMODE_H
#define UMODE_H

#include "types.h"

void user_mode_begin(void);
void user_mode_set_return(void (*fn)(void));
void user_mode_end_from_exit(int status);
void user_mode_preserve(void);
void user_mode_restore(void);
extern volatile int user_mode_exit_flag;
void user_mode_enter(uint64_t entry, uint64_t stack_top);
int  user_mode_active(void);
int  user_mode_last_exit_status(void);

#endif

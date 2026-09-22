#ifndef SCHED_SCHEDULER_H
#define SCHED_SCHEDULER_H

void sched_mod_init(void);
int  sched_mod_spawn(const char *name, void (*entry)(void));
void sched_mod_yield(void);
int  sched_mod_thread_count(void);

#endif

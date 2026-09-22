/* Modular scheduler API — delegates to kernel/sched.c */

#include "scheduler.h"
#include "kernel/sched.h"

void sched_mod_init(void) {
    sched_init();
}

int sched_mod_spawn(const char *name, void (*entry)(void)) {
    return sched_create_thread(name, entry);
}

void sched_mod_yield(void) {
    sched_yield();
}

int sched_mod_thread_count(void) {
    return sched_thread_count();
}

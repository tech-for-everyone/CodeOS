#ifndef SCHED_TASK_H
#define SCHED_TASK_H

#include "types.h"

#define SCHED_TASK_NAME_MAX 32

typedef enum {
    SCHED_TASK_READY,
    SCHED_TASK_RUNNING,
    SCHED_TASK_BLOCKED,
    SCHED_TASK_ZOMBIE
} sched_task_state_t;

typedef struct {
    char name[SCHED_TASK_NAME_MAX];
    sched_task_state_t state;
    int tid;
} sched_task_t;

#endif

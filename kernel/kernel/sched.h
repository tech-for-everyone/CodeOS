#ifndef SCHED_H
#define SCHED_H

#include "types.h"
#include "idt.h"
#include "process.h"

#define THREAD_NAME_MAX 32
#define THREAD_MAX 64
#define STACK_SIZE 131072
/* Syscall stacks are smaller than kernel stacks; 16KB fits the nested
 * network TX/RX buffer chain (UDP pkt + IP frame + Ethernet frame). */
#define SYSCALL_STACK_SIZE 16384

/* Priority levels: 0 = idle, 1 = low, 2 = normal, 3 = high, 4 = realtime */
#define SCHED_PRIO_IDLE     0
#define SCHED_PRIO_LOW      1
#define SCHED_PRIO_NORMAL   2
#define SCHED_PRIO_HIGH     3
#define SCHED_PRIO_REALTIME 4
#define SCHED_PRIO_COUNT    5

/**
 * enum thread_state - Thread lifecycle states.
 * @THREAD_READY:   On the run queue.
 * @THREAD_RUNNING: Currently executing on a CPU.
 * @THREAD_BLOCKED: Waiting for an event (I/O, sleep, lock).
 * @THREAD_ZOMBIE:  Exited, awaiting cleanup.
 */
typedef enum { THREAD_READY, THREAD_RUNNING, THREAD_BLOCKED, THREAD_ZOMBIE } thread_state_t;

/**
 * struct thread - Kernel thread state.
 *
 * Each thread has its own kernel stack and FPU state, but shares
 * the address space of its owning process.
 */
typedef struct thread {
    char name[THREAD_NAME_MAX];
    thread_state_t state;
    uint64_t *stack;
    uint64_t *kernel_stack_top;
    uint64_t *syscall_stack_top;
    int tid;
    int priority;
    uint64_t sleep_until;
    uint64_t cpu_time;
    struct thread *next;
    struct thread *ready_next;  /* for per-priority ready queue */
    char fpu_state[512] __attribute__((aligned(16)));
} thread_t;

/* ── Scheduler API ── */
void sched_init(void);
int  sched_create_thread(const char *name, void (*entry)(void));
int  sched_create_thread_prio(const char *name, void (*entry)(void), int priority);
void sched_yield(void);
void sched_preempt_disable(void);
void sched_preempt_enable(void);
void sched_exit(int code);
void sched_sleep_ms(uint64_t ms);
thread_t *sched_current(void);
void sched_timer_tick(int_frame_t *frame);
int  sched_thread_count(void);
uint64_t sched_busy_ticks(void);   /* total non-idle scheduler ticks */
void sched_set_priority(int tid, int priority);

void sched_switch_to_process(process_t *p);

extern volatile int need_resched;

#endif

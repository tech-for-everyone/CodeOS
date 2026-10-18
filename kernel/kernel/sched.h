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

    /* ── Process association ──
     *
     * Processes are executed by threads: a thread that is running a user
     * program on behalf of process @proc carries a pointer to it, and the
     * scheduler moves current_process, CR3 and TSS.RSP0 with the thread.
     * A kernel thread that has never entered user mode carries 0.  The
     * owning thread of a fork()ed child is created with owns_proc = 1 and
     * dies with the process (its user-mode exit retires the thread), while a
     * kernel thread that merely borrows a process for a nested session
     * (cmd_exec, container start) keeps owns_proc = 0 and survives the
     * program's exit.  rsp0_top is this thread's TSS.RSP0 stack top, lazily
     * allocated on first user-mode entry (UMODE_KSTACK_SIZE bytes). */
    struct process *proc;
    int owns_proc;
    uint64_t rsp0_top;

    /* ── Per-thread user-mode session state ──
     *
     * The entry/exit machinery (umode_entry.S, umode.c) keeps its state in
     * globals: the return leaf, the kernel stack to return on, the
     * callee-saved registers, the saved user rsp for the LSTAR path.  That
     * was sound while exactly one thread could be inside a user session at a
     * time.  A fork()ed child breaks that -- it runs concurrently with its
     * (blocked) parent -- so each live session owns its copy and
     * sched_yield() swaps them with umode_switch_save()/umode_switch_load()
     * on every context switch.
     *
     * syscall_frame is the int $0x80 trapframe of the syscall this thread is
     * currently inside (0 otherwise).  proc_fork() captures the parent's user
     * context from it and execve() redirects it; it lives on this thread's
     * ring-0 stack, so it stays valid across the handler blocking (proc_wait)
     * and across switches. */
    int_frame_t *syscall_frame;
    uint64_t umode_return_rip;
    uint64_t umode_kernel_rsp;
    uint64_t umode_return_override;
    uint64_t umode_saved_rbx, umode_saved_rbp, umode_saved_r12;
    uint64_t umode_saved_r13, umode_saved_r14, umode_saved_r15;
    uint64_t umode_saved_user_rsp;
    int umode_running;
    int umode_exit_status;
    int umode_exit_flag;
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

/* Create a thread that is the execution context of @proc (a fork()ed child).
 * The thread carries proc in thread_t::proc, is scheduled normally, and dies
 * when the process exits (proc_thread_exit_leaf). */
int sched_create_thread_proc(const char *name, void (*entry)(void),
                             process_t *proc);

/* Retire the thread that owns @proc (proc_kill's SIGKILL/SIGTERM path): a
 * process must not be reaped while a thread that can still be scheduled
 * points at it.  The caller is another thread on this single CPU, so the
 * target is only blocked or ready; it is dequeued and marked zombie. */
void sched_retire_proc_thread(process_t *proc);

extern volatile int need_resched;

#endif

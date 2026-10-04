#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "vmm.h"
#include "elf.h"

#define PROC_NAME_MAX 32
#define PROC_MAX 64
#define PROC_FD_MAX 256
#define PROC_PIPE_BUF 4096
#define PROC_NS_MAX 8  /* one per namespace type */

/* Security levels for the task manager.
 *
 * Higher numbers are higher privilege.  Enforcement is asymmetric:
 * a task may only interfere with (kill, inspect, signal) tasks at
 * a *lower* level.  This keeps level-3 kernel apps (android-containers,
 * the X11 window protocol) safe from level-2 programs and below, and
 * keeps containerized (level-0) workloads isolated from everything
 * that isn't explicitly authorized.
 *
 *   0  container  — containerized env, potentially dangerous
 *   1  os         — run by the actual OS, still high security
 *   2  user       — most programs; hyperde talks to systemm which
 *                    talks to the kernel at this level
 *   3  kernel     — kernel apps, high priority: android-containers,
 *                    x11 window protocol, etc.
 */
typedef enum {
    LEVEL_CONTAINER = 0,
    LEVEL_OS,
    LEVEL_USER,
    LEVEL_KERNEL
} proc_level_t;

/* Virtual address layout — must agree with VMM and linker. */
#define PROC_BRK_BASE  0x60000000UL
#define PROC_BRK_MAX   0x70000000UL
#define PROC_MMAP_BASE 0x7F00000000ULL

/**
 * enum proc_state - Process lifecycle states.
 * @PROC_CREATED: Allocated but not yet scheduled.
 * @PROC_READY:   On the run queue, waiting for a timeslice.
 * @PROC_RUNNING: Currently executing on a CPU.
 * @PROC_SLEEPING: Blocked on I/O or voluntary yield.
 * @PROC_ZOMBIE:  Exited but not yet reaped by parent.
 * @PROC_DEAD:    Slot is free for reuse.
 */
typedef enum {
    PROC_CREATED,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
    PROC_DEAD
} proc_state_t;

/**
 * struct pipe - Kernel pipe buffer.
 * @buf:    Circular byte buffer of size PROC_PIPE_BUF.
 * @rpos:   Read cursor position.
 * @wpos:   Write cursor position.
 * @readers: Number of open reader file descriptors.
 * @writers: Number of open writer file descriptors.
 * @open:   Non-zero while the pipe is alive.
 */
typedef struct pipe {
    uint8_t buf[PROC_PIPE_BUF];
    int rpos, wpos;
    int readers, writers;
    int open;
} pipe_t;

typedef struct proc_fd {
    int type;
    int inode;
    int pos;
    int flags;
    pipe_t *pipe;
} proc_fd_t;

/**
 * struct proc_ctx - Saved user-mode CPU context for one process.
 *
 * Captured from the interrupt frame at the instant a process is switched out,
 * so restoring it and executing iretq resumes that process exactly where it
 * left off. This is what makes fork() possible: the child needs its own copy
 * of the parent's context with rax forced to 0, so it resumes inside fork()
 * and returns 0 while the parent resumes with the child's pid.
 *
 * All-zero with @has_ctx clear means "this process has never been switched
 * out", which is the state of every process under the old single-context
 * design. Nothing may iretq out of such a context.
 */
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, cs, rflags, rsp, ss;
} proc_ctx_t;

/**
 * struct process - Per-process state block.
 *
 * Contains everything the kernel needs to context-switch, manage memory,
 * and track file descriptors for a single process.
 */
typedef struct process {
    int pid;
    int ppid;
    char name[PROC_NAME_MAX];
    proc_state_t state;
    int exit_status;

    /* memory */
    uint64_t *pml4;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t brk;
    uint64_t mmap_base;

    /* file descriptors */
    proc_fd_t fds[PROC_FD_MAX];

    /* scheduling */
    uint64_t kernel_rsp;
    uint64_t syscall_rsp;
    int preempt_ticks;

    /* Saved user-mode context, valid only when has_ctx is set. Saved on
     * switch-out, restored on switch-in. */
    proc_ctx_t ctx;
    int has_ctx;

    /* This process's own kernel stack for the syscall path.
     *
     * NOT TSS.RSP0. Syscalls do not go through int 0x80 here: arch/
     * syscall_entry.S is the LSTAR handler for the `syscall` instruction, and
     * its first instruction switches to the single global syscall_kernel_rsp.
     * So every process shares one stack today, and a per-process stack has to
     * be swapped into that global on each switch or two processes would
     * overwrite each other's trapframes. TSS.RSP0 is a separate problem: it is
     * what IRQs and exceptions land on, and user_mode_enter() allocates 16
     * fresh pages there on every entry instead of storing it per process (and
     * leaks the previous one). Sized like thread_t's, via SYSCALL_STACK_SIZE.
     * 0 == not allocated yet. */
    uint64_t kstack_top;
    uint64_t kstack_pages;

    /* personality (syscall translation) */
    int personality;

    /* Terminal discipline: non-zero once the process clears ICANON via
     * TCSETS, which makes sys_read() deliver keystrokes as they arrive
     * instead of line-buffering them. Scoped per-process on purpose — a
     * global would let a raw-mode app leave the shell in raw mode after it
     * exits, since fd 0 is shared. */
    int tty_raw;

    /* namespace membership (one per namespace type) */
    int namespaces[PROC_NS_MAX];

    /* cgroup membership */
    int cgroup_id;

    /* UID/GID (for user namespace) */
    int uid, gid;
    int euid, egid;

    /* security level (0=container, 1=os, 2=user, 3=kernel).
     * A task may only interfere with tasks at a *lower* level. */
    proc_level_t level;

    /* process tree */
    struct process *next;
    struct process *children;
    struct process *sibling;
    struct process *parent;
} process_t;

/* ── Process lifecycle ── */
int proc_init(void);
int proc_create(const char *name, uint64_t entry, uint64_t stack_top,
                proc_level_t level);
int proc_create_level(const char *name, uint64_t entry, uint64_t stack_top,
                proc_level_t level);
int proc_fork(void);
uint64_t proc_exec(uint64_t entry, uint64_t stack_top, int argc, char **argv, char **envp, elf_auxv_info_t *auxv);
int proc_exit(int status);
int proc_wait(int pid, int *status);
int proc_kill(int pid, int sig);
void proc_reap(void);
process_t *proc_current(void);
process_t *proc_get(int pid);
int proc_getpid(void);
int proc_getppid(void);

/* ── Process run queue / context switching ──
 *
 * The kernel has two independent schedulers: thread_t (kernel threads, each
 * with its own stacks) and process_t (userspace address spaces). Until the
 * fields above existed, current_process was assigned exactly once — by
 * proc_create(), and only when no process was current — so a forked child
 * could never run: proc_fork() built the child and returned its pid, and
 * nothing ever made it current. sched_switch_to_process() was the only code
 * that could, and it had no callers at all.
 *
 * An earlier version of this change also added proc_sched_runnable() /
 * proc_sched_pick() plus a run_next linked list, and both were reverted. The
 * accessors had no callers, so --gc-sections dropped them from the image
 * (`nm codeos-1-kernel.bin | grep -c proc_sched` -> 0) — shipping uncalled
 * functions is how jengine ended up as build-verified-but-dead. The list was
 * never read, because the picker scanned the table. It was wrong besides: the
 * "already queued" test (run_next != 0) misses a lone element or a tail, the
 * tail search skipped run_next == 0 entries so it found the second-to-last
 * node and orphaned the real last one, and with an empty or single-element
 * queue the new process was never linked in at all.
 *
 * Readiness is therefore the predicate
 * "state == PROC_READY && p != current_process" over proc_table; PROC_MAX is
 * 64, so a linear scan is a few hundred instructions. Add a run-queue list
 * only alongside a real consumer and a head/tail invariant.
 *
 * The fields above are inert until a switch path is wired to them.
 */

/* ── File descriptor management ── */
int proc_fd_alloc(void);
int proc_fd_close(int fd);
proc_fd_t *proc_fd_get(int fd);
int proc_fd_dup(int oldfd);
int proc_fd_dup2(int oldfd, int newfd);
int shm_create(int size);
void *shm_map(int fd);

/* ── Pipes ── */
int pipe_create(int fd[2]);
int pipe_read(pipe_t *p, uint8_t *buf, int len);
int pipe_write(pipe_t *p, const uint8_t *buf, int len);
void pipe_close(pipe_t *p, int writer);

extern process_t *current_process;
extern process_t proc_table[PROC_MAX];
extern int next_pid;

/* ── Task / security levels ──
 * These are the raw accessors: read a level, set a level.  They do not
 * enforce anything.  The rule for who may act on whom, and the code that
 * enforces it, is in systemm.h -- the shell builtin and the kill/tkill/tgkill
 * syscalls all go through there so there is one implementation.  Do not add a
 * second authorization check here. */
int  proc_set_level(int pid, proc_level_t level);
proc_level_t proc_get_level(int pid);
int  proc_list(void);
int  proc_exists(int pid);
int  proc_get_pid_name(int pid, char *buf, int len);

#endif

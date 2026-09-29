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

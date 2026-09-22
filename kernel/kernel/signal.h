#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"

/* ── POSIX signal numbers ── */
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGSTKFLT 16
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGURG   23
#define SIGXCPU  24
#define SIGXFSZ  25
#define SIGVTALRM 26
#define SIGPROF  27
#define SIGWINCH 28
#define SIGIO    29
#define SIGPWR   30
#define SIGSYS   31
#define SIGNAL_MAX 64

/* ── Signal action constants ── */
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

/* sa_flags */
#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_ONSTACK    0x00000008
#define SA_RESTART    0x00000010
#define SA_NODEFER    0x00000020
#define SA_RESETHAND  0x00000040
#define SA_NOMASK     SA_NODEFER
#define SA_THANDLER   SA_RESETHAND

/* Signal disposition */
#define SIG_DISPOSITION_DEFAULT  0
#define SIG_DISPOSITION_IGNORE   1
#define SIG_DISPOSITION_HANDLER  2

/* ── Types ── */

typedef struct {
    uint64_t bits[2];  /* 64 signals */
} sigset_t;

typedef struct {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, void *, void *);
    sigset_t sa_mask;
    int sa_flags;
} sigaction_t;

typedef struct sig_queue_entry {
    int sig;
    int sender_pid;
    struct sig_queue_entry *next;
} sig_queue_entry_t;

/* Per-process signal state (embedded in process_t or maintained separately) */
typedef struct {
    sigset_t pending;       /* pending standard signals (bitmask) */
    sigset_t blocked;       /* blocked signals (bitmask) */
    sigset_t ignored;       /* ignored signals (bitmask) */
    sigaction_t actions[SIGNAL_MAX];
    sig_queue_entry_t *queue;  /* queued real-time signals */
    int queue_len;
    int queue_max;
} proc_signal_state_t;

/* ── API ── */
void signal_init(proc_signal_state_t *state);
void signal_free(proc_signal_state_t *state);
int  signal_send(int pid, int sig);
int  signal_send_group(int pgid, int sig);
int  signal_send_thread(int pid, int tid, int sig);
int  signal_action(int sig, const sigaction_t *act, sigaction_t *oldact);
int  signal_mask(int how, const sigset_t *set, sigset_t *oset);
int  signal_pending(proc_signal_state_t *state);
int  signal_deliver(proc_signal_state_t *state);
void signal_queue_free(proc_signal_state_t *state);
int  signal_is_blocked(proc_signal_state_t *state, int sig);
int  signal_set_ignored(proc_signal_state_t *state, int sig, int ignore);
int  signal_default_action(int sig);
int  signal_suspend(const sigset_t *mask);
int  signal_timedwait(const sigset_t *mask, uint64_t timeout_ms);
void signal_coredump(int sig);
void signal_child_notify(int parent_pid, int child_pid, int status);

/* Signal set helpers */
void sigemptyset(sigset_t *set);
void sigfillset(sigset_t *set);
void sigaddset(sigset_t *set, int sig);
void sigdelset(sigset_t *set, int sig);
int  sigismember(const sigset_t *set, int sig);
int  sigisempty(const sigset_t *set);
void sigorset(sigset_t *dst, const sigset_t *src);
void sigandset(sigset_t *dst, const sigset_t *src);
int  sigset_count(const sigset_t *set);

typedef void (*sighandler_t)(int);
extern sighandler_t signal(int sig, sighandler_t handler);

#endif

#include "signal.h"
#include "process.h"
#include "sched.h"
#include "mm.h"
#include "kprintf.h"
#include "string.h"

void sigemptyset(sigset_t *s) { s->bits[0] = s->bits[1] = 0; }
void sigfillset(sigset_t *s) { s->bits[0] = s->bits[1] = ~0ULL; }
void sigaddset(sigset_t *s, int sig) {
    if (sig <= 0 || sig > SIGNAL_MAX) return;
    if (sig <= 64) s->bits[0] |= (1ULL << (sig-1)); else s->bits[1] |= (1ULL << (sig-65));
}
void sigdelset(sigset_t *s, int sig) {
    if (sig <= 0 || sig > SIGNAL_MAX) return;
    if (sig <= 64) s->bits[0] &= ~(1ULL << (sig-1)); else s->bits[1] &= ~(1ULL << (sig-65));
}
int sigismember(const sigset_t *s, int sig) {
    if (sig <= 0 || sig > SIGNAL_MAX) return 0;
    if (sig <= 64) return (int)((s->bits[0] >> (sig-1)) & 1);
    return (int)((s->bits[1] >> (sig-65)) & 1);
}
int sigisempty(const sigset_t *s) { return !s->bits[0] && !s->bits[1]; }
void sigorset(sigset_t *d, const sigset_t *s) { d->bits[0]|=s->bits[0]; d->bits[1]|=s->bits[1]; }
void sigandset(sigset_t *d, const sigset_t *s) { d->bits[0]&=s->bits[0]; d->bits[1]&=s->bits[1]; }
int sigset_count(const sigset_t *s) {
    int c=0; uint64_t v=s->bits[0];
    while(v){c+=(int)(v&1);v>>=1;} v=s->bits[1];
    while(v){c+=(int)(v&1);v>>=1;} return c;
}

static proc_signal_state_t signal_states[64];

void signal_init(proc_signal_state_t *st) {
    if (!st) return;
    sigemptyset(&st->pending); sigemptyset(&st->blocked); sigemptyset(&st->ignored);
    for (int i = 0; i < SIGNAL_MAX; i++) {
        st->actions[i].sa_handler = SIG_DFL;
        st->actions[i].sa_sigaction = 0;
        sigemptyset(&st->actions[i].sa_mask);
        st->actions[i].sa_flags = 0;
    }
    st->queue = 0; st->queue_len = 0; st->queue_max = 128;
}

void signal_free(proc_signal_state_t *st) {
    if (!st) return;
    sig_queue_entry_t *e = st->queue;
    while (e) { sig_queue_entry_t *n = e->next; free(e); e = n; }
    st->queue = 0; st->queue_len = 0;
}

int signal_default_action(int sig) {
    switch (sig) {
    case SIGHUP: case SIGINT: case SIGQUIT: case SIGKILL: case SIGUSR1:
    case SIGUSR2: case SIGPIPE: case SIGALRM: case SIGTERM: case SIGPWR:
        return 1;
    case SIGCONT: case SIGCHLD: case SIGURG: case SIGWINCH: case SIGIO:
        return 0;
    case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
        return 2;
    case SIGILL: case SIGTRAP: case SIGABRT: case SIGBUS: case SIGFPE:
    case SIGSEGV: case SIGPROF: case SIGVTALRM: case SIGSYS:
        return 3;
    }
    return 1;
}

int signal_send(int pid, int sig) {
    if (sig <= 0 || sig > SIGNAL_MAX) return -1;
    if (sig == SIGKILL || sig == SIGSTOP) return proc_kill(pid, sig);
    process_t *t = proc_get(pid);
    if (!t) return -1;
    if (sig <= 64) signal_states[pid%64].pending.bits[0] |= (1ULL<<(sig-1));
    else signal_states[pid%64].pending.bits[1] |= (1ULL<<(sig-65));
    if (t->state == PROC_SLEEPING) t->state = PROC_READY;
    return 0;
}

int signal_send_group(int pgid, int sig) {
    int sent = 0;
    (void)pgid;
    for (int i = 0; i < 64; i++)
        if (proc_table[i].state != PROC_DEAD && proc_table[i].pid > 0)
            if (signal_send(proc_table[i].pid, sig) == 0) sent++;
    return sent;
}

int signal_send_thread(int pid, int tid, int sig) { (void)tid; return signal_send(pid, sig); }

int signal_action(int sig, const sigaction_t *act, sigaction_t *old) {
    if (sig<=0||sig>SIGNAL_MAX||sig==SIGKILL||sig==SIGSTOP) return -1;
    process_t *p = current_process;
    if (!p) return -1;
    proc_signal_state_t *st = &signal_states[p->pid%64];
    if (old) *old = st->actions[sig-1];
    if (act) st->actions[sig-1] = *act;
    return 0;
}

int signal_mask(int how, const sigset_t *set, sigset_t *oset) {
    process_t *p = current_process;
    if (!p) return -1;
    proc_signal_state_t *st = &signal_states[p->pid%64];
    if (oset) *oset = st->blocked;
    if (!set) return 0;
    switch (how) {
    case 1: sigorset(&st->blocked, set); break;
    case 2: { sigset_t inv=*set; inv.bits[0]=~inv.bits[0]; inv.bits[1]=~inv.bits[1]; sigandset(&st->blocked,&inv); break; }
    case 3: st->blocked = *set; break;
    default: return -1;
    }
    return 0;
}

int signal_pending(proc_signal_state_t *st) { return st ? sigset_count(&st->pending) : 0; }
int signal_is_blocked(proc_signal_state_t *st, int sig) {
    if (!st||sig<=0||sig>SIGNAL_MAX) return 0;
    return sigismember(&st->blocked, sig);
}
int signal_set_ignored(proc_signal_state_t *st, int sig, int ign) {
    if (!st||sig<=0||sig>SIGNAL_MAX) return -1;
    if (ign) sigaddset(&st->ignored, sig); else sigdelset(&st->ignored, sig); return 0;
}

int signal_deliver(proc_signal_state_t *st) {
    if (!st) return 0;
    process_t *p = current_process;
    if (!p) return 0;
    for (int sig = 1; sig <= SIGNAL_MAX; sig++) {
        if (!sigismember(&st->pending,sig)) continue;
        if (signal_is_blocked(st,sig)) continue;
        if (sigismember(&st->ignored,sig)) { sigdelset(&st->pending,sig); continue; }
        int act = signal_default_action(sig);
        if (st->actions[sig-1].sa_handler==SIG_IGN) { sigdelset(&st->pending,sig); continue; }
        sigdelset(&st->pending, sig);
        if (act==1||act==3) {
            if (act==3) signal_coredump(sig);
            p->state=PROC_ZOMBIE; p->exit_status=sig;
            if (p->parent&&p->parent->state==PROC_SLEEPING) p->parent->state=PROC_READY;
            return sig;
        }
        if (act==2) { p->state=PROC_SLEEPING; return 0; }
        if (st->actions[sig-1].sa_handler && st->actions[sig-1].sa_handler!=SIG_DFL
            && st->actions[sig-1].sa_handler!=SIG_IGN)
            st->actions[sig-1].sa_handler(sig);
    }
    return 0;
}

void signal_queue_free(proc_signal_state_t *st) {
    if (!st) return;
    sig_queue_entry_t *e = st->queue;
    while (e) { sig_queue_entry_t *n=e->next; free(e); e=n; }
    st->queue=0; st->queue_len=0;
}

void signal_coredump(int sig) {
    process_t *p = current_process;
    kprintf("signal: CORE DUMP pid=%d sig=%d name=%s\n", p?p->pid:-1, sig, p?p->name:"?");
}

void signal_child_notify(int parent_pid, int child_pid, int status) {
    (void)child_pid; (void)status; signal_send(parent_pid, SIGCHLD);
}

int signal_suspend(const sigset_t *mask) {
    process_t *p = current_process;
    if (!p) return -1;
    sigset_t old; signal_mask(3, mask, &old);
    p->state = PROC_SLEEPING; sched_yield();
    signal_mask(3, &old, 0); return 0;
}

int signal_timedwait(const sigset_t *mask, uint64_t timeout_ms) {
    (void)mask;
    process_t *p = current_process;
    if (!p) return -1;
    p->state = PROC_SLEEPING; sched_sleep_ms(timeout_ms);
    return 0;
}

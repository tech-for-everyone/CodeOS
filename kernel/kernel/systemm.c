/**
 * systemm — the single authority on task privilege.
 *
 * See systemm.h for the model. This file exists so the shell builtin and the
 * syscall handlers cannot disagree about who may act on what: the builtin
 * in shell.c calls straight into here rather than deciding for itself.
 */

#include "systemm.h"
#include "kprintf.h"
#include "string.h"

int systemm_level_of(int pid) {
    /* proc_exists() is checked first so "no such task" is reported as -1
     * rather than falling through to proc_get_level()'s refuse-by-default
     * LEVEL_KERNEL.  Both deny, but only one of them is honest about why. */
    if (!proc_exists(pid)) return -1;
    proc_level_t lv = proc_get_level(pid);
    if (lv < LEVEL_CONTAINER || lv > LEVEL_KERNEL) return -1;
    return (int)lv;
}

const char *systemm_level_name(int level) {
    switch (level) {
        case LEVEL_CONTAINER: return "container";
        case LEVEL_OS:        return "os";
        case LEVEL_USER:      return "user";
        case LEVEL_KERNEL:    return "kernel";
        default:              return "?";
    }
}

int systemm_caller_level(void) {
    /* current_process is the caller.  That is only sound because of an
     * invariant in the scheduler: sched_timer_tick() clears current_process
     * only in a branch that already requires need_resched (sched.c), and
     * isr_dispatch_c() acts on need_resched by sched_yield()ing before it
     * returns to user mode (idt.c).  So a process that is running user code
     * always has current_process set.
     *
     * If that invariant is ever broken, this returns -1 and every check
     * below denies -- fail-closed rather than fail-open, which is the
     * direction that matters if it ever regresses. */
    process_t *p = proc_current();
    if (!p) return -1;
    int lv = (int)p->level;
    if (lv < LEVEL_CONTAINER || lv > LEVEL_KERNEL) return -1;
    return lv;
}

int systemm_may_act(int actor_level, int target_pid) {
    /* An out-of-range actor is refused rather than clamped.  Clamping would
     * turn a corrupted level into LEVEL_CONTAINER, i.e. into the weakest
     * possible authority, and "the lower level" is the direction that is
     * allowed everywhere. */
    if (actor_level < LEVEL_CONTAINER || actor_level > LEVEL_KERNEL)
        return 0;

    int target = systemm_level_of(target_pid);
    if (target < 0) return 0;   /* no such task */

    /* At or below your own level, never above. */
    return actor_level >= target;
}

int systemm_caller_may_act(int target_pid) {
    int actor = systemm_caller_level();
    if (actor < 0) return 0;    /* no caller: no authority */
    return systemm_may_act(actor, target_pid);
}

int systemm_kill(int actor_level, int target_pid, int sig) {
    if (!systemm_may_act(actor_level, target_pid)) return -1;   /* EPERM */
    return proc_kill(target_pid, sig);
}

int systemm_kill_from_caller(int target_pid, int sig) {
    int actor = systemm_caller_level();
    if (actor < 0) return -1;
    return systemm_kill(actor, target_pid, sig);
}

int systemm_set_level(int actor_level, int target_pid, int level) {
    if (level < LEVEL_CONTAINER || level > LEVEL_KERNEL) return -1;
    if (systemm_level_of(target_pid) < 0) return -1;

    /* No promotion, not even to your own level.  Re-levelling is how a task
     * would otherwise climb: a level-2 shell could set itself to 3 and then
     * pass every subsequent check.  Leaving a task where it is, or moving it
     * down, is not an escalation. */
    if (level > actor_level) return -1;

    return proc_set_level(target_pid, (proc_level_t)level);
}

/* Iterate the process table and print every live task with its level.
 * Returns the count of live tasks.
 *
 * This lives here rather than in process.c so that the level-name table has a
 * single definition: process.c is the layer below this one and must not call
 * back up into it, so a table there could not be shared with systemm_level_name
 * without inverting the dependency.  The declaration stays in process.h
 * because that is where callers already expect it. */
int proc_list(void) {
    int n = 0;
    kprintf("  PID  NAME                   STATE      LEVEL\n");
    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_DEAD || p->state == 0) continue;
        const char *st;
        switch (p->state) {
            case PROC_CREATED: st = "created"; break;
            case PROC_READY:   st = "ready";   break;
            case PROC_RUNNING: st = "running"; break;
            case PROC_SLEEPING:st = "sleeping";break;
            case PROC_ZOMBIE:  st = "zombie";  break;
            default:           st = "?";       break;
        }
        /* systemm_level_name() range-checks for us, so a corrupted level
         * prints as "?" instead of indexing off the end of a table. */
        int lv = (int)p->level;
        kprintf("  %4d %-22s %-10s %d %s\n", p->pid, p->name, st, lv,
                systemm_level_name(lv));
        n++;
    }
    if (n == 0) kprintf("  (no tasks)\n");
    return n;
}

#ifndef SYSTEMM_H
#define SYSTEMM_H

/**
 * systemm — the single authority on task privilege.
 *
 * Every user-visible way to act on another task goes through this file: the
 * shell builtin in shell.c, and the kill/tkill/tgkill syscall handlers. The
 * point is that there is exactly one implementation of "may this task do
 * that", so a program cannot route around the shell builtin by calling the
 * syscall directly — which is exactly what it did before this existed.
 *
 * The four levels are ordered by privilege, and the ordering is the whole
 * permission model: a task may act on anything at or below its own level and
 * never on anything above it. There is no separate ACL to keep in sync.
 *
 *   0  container  containerized env, potentially dangerous
 *   1  os         run by the actual OS, still high security
 *   2  user       most programs; hyperde -> systemm -> kernel
 *   3  kernel     android-containers, x11 window protocol, etc.
 */

#include "types.h"
#include "process.h"

/* ── Queries ── */

/* Level of a task, as a plain number so callers can compare without the enum.
 * Returns 0..3 for a live task, or -1 if no such task exists.  Callers must
 * treat -1 as "deny": an unreachable pid is not a level, and answering with
 * a low level for it would make `kill <any number>` permitted. */
int systemm_level_of(int pid);

/* "container" / "os" / "user" / "kernel" / "?" — never NULL. */
const char *systemm_level_name(int level);

/* Level of the calling task, or -1 when there is no calling task (early
 * boot, or a context where the kernel is acting for itself). */
int systemm_caller_level(void);

/* ── The check ── */

/* May a task at `actor_level` act on `target_pid`?
 * Returns 1 to allow, 0 to deny. Denies when the target does not exist, when
 * either level is out of range, and when the target is strictly above the
 * actor. Acting on your own level is allowed: a task may manage its peers. */
int systemm_may_act(int actor_level, int target_pid);

/* may_act() with the caller's level filled in. This is the form the syscall
 * handlers use. Returns 0 when there is no caller, which denies — an
 * unattributed action has no authority behind it. */
int systemm_caller_may_act(int target_pid);

/* ── Actions ──
 * Each of these enforces the level rule and then performs the operation, so
 * callers cannot get the authorization and the effect out of step.
 *
 * Return value is the operation's own: 0 on success, -1 on failure. A refusal
 * is -1, the same as a bad pid, because the kernel has no shared errno header
 * (syscall.c defines LINUX_EPERM locally) and inventing one here would tie
 * this file to the Linux personality. Callers that need to tell "refused"
 * from "no such task" ask systemm_level_of() first; the syscall layer
 * translates the refusal to -EPERM at its own boundary. */

int systemm_kill(int actor_level, int target_pid, int sig);
int systemm_set_level(int actor_level, int target_pid, int level);

/* Convenience wrappers for the syscall handlers: authorize against the
 * calling task, then act. */
int systemm_kill_from_caller(int target_pid, int sig);

#endif /* SYSTEMM_H */

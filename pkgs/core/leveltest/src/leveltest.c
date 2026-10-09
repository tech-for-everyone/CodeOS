/* leveltest -- verify that the security level actually gates the kill syscall.
 *
 * The `systemm task` shell builtin is not what enforces the level rule; the
 * kill syscall handler is.  This program calls sys_kill() the way any
 * ordinary program would, so it exercises the real path.  Run it at different
 * levels with `systemm task run --level N /bin/leveltest <pid>` and the
 * results should differ, which is the whole point.
 *
 * Signal 0 is used throughout: it performs the permission check and delivers
 * nothing, so the test cannot kill the machine by mistyping a pid.
 */
#include "unistd.h"
#include "stdio.h"
#include "string.h"

static int enter_container(int argc, char **argv);
static int check_gate(int argc, char **argv);

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--into") == 0)
        return enter_container(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "--gate") == 0)
        return check_gate(argc, argv);

    int me = sys_getpid();
    printf("leveltest: pid %d\n", me);

    /* Self-signal. Must always be allowed: a task may act on its own level,
     * and this is the case that would break first if the rule were
     * accidentally strict (>) instead of inclusive (>=). */
    int r_self = sys_kill(me, 0);
    printf("self   kill(%d,0) = %d  %s\n", me, r_self,
           r_self == 0 ? "ALLOWED" : "REFUSED");

    /* A named target, if one was given. */
    if (argc >= 2) {
        int target = 0;
        for (const char *p = argv[1]; *p; p++) {
            if (*p < '0' || *p > '9') break;
            target = target * 10 + (*p - '0');
        }
        if (target <= 0) {
            printf("target: '%s' is not a pid\n", argv[1]);
            return 2;
        }
        int r = sys_kill(target, 0);
        printf("target kill(%d,0) = %d  %s\n", target, r,
               r == 0 ? "ALLOWED" : "REFUSED");

        /* -1 is the kernel's generic failure; the syscall layer maps a
         * refusal to -EPERM (1) and a missing pid to -ESRCH (3), so the
         * value distinguishes "you may not" from "it is not there". */
        if (r == 0)        printf("  -> permitted\n");
        else if (r == -1)  printf("  -> refused (-EPERM) or no such task\n");
        else if (r == -3)  printf("  -> no such task (-ESRCH)\n");
        else if (r == -22) printf("  -> bad pid (-EINVAL)\n");
        else               printf("  -> rc=%d\n", r);
    }

    /* A pid that certainly does not exist, to pin down the ESRCH path. */
    int r_missing = sys_kill(9999, 0);
    printf("absent kill(9999,0) = %d  %s\n", r_missing,
           r_missing == -3 ? "ESRCH" : (r_missing == 0 ? "ALLOWED(BUG)" : "other"));

    /* Optional second argument: stay alive that many milliseconds. A task's
     * level is only observable while it exists, and the processes worth
     * comparing against (a container initializer, another container) exit or
     * block, so holding this one open is what makes the comparison possible
     * from the shell. */
    if (argc >= 3) {
        int ms = 0;
        for (const char *p = argv[2]; *p; p++) {
            if (*p < '0' || *p > '9') break;
            ms = ms * 10 + (*p - '0');
        }
        if (ms > 0) {
            printf("holding for %d ms\n", ms);
            sys_sleep(ms);
        }
    }
    return 0;
}

/* leveltest --gate <name> -- did this level actually get to create a container?
 *
 * This exercises level_gate(), the gate in syscall.c that fronts the
 * container/VM state-changing syscalls. It is a different enforcement point
 * from gated_kill(), which the rest of this file covers, so nothing else here
 * touches it.
 *
 * The discriminator is the return value, because container_create() returns
 * the new container's id on success and -1 on every failure, while level_gate()
 * returns -EPERM. A positive id is therefore unambiguous proof the gate let the
 * call through; there is no other way to get one.
 *
 * A negative result is NOT proof of a refusal on its own, and this program
 * does not claim it is. -1 is also what "already exists", "no slots
 * available" and "unknown image" return, so a lone "-1" is consistent with a
 * gate that is wide open and a broken image path. Pair it with a control:
 *
 *   systemm task run --level 2 /bin/leveltest --gate lvl2probe
 *   container create ctrlprobe android-stock
 *   container list -a
 *
 * The shell's `container` builtin calls container_create() directly, bypassing
 * the gate, so a successful `container create` in the same session proves the
 * image path, slot table and namespace setup are all fine. Same binary, same
 * image, same moment -- the gate is the only variable. If the control fails,
 * this test proved nothing and should be reported as inconclusive.
 *
 * Note that the level-3 run is *not* available as a control: the shell itself
 * is level 2, and `task run --level 3` is refused with "level 2 (user) may
 * not start a task at level 3 (kernel)". That refusal is the anti-escalation
 * rule working, and it is why the builtin is the control instead.
 *
 * Do not use sys_container_list() to check the result. It calls
 * container_list(), which only returns CONTAINER_RUNNING containers, so a
 * container that was created but never started is invisible by design and the
 * test would report "absent" even when creation plainly succeeded.
 */
static int check_gate(int argc, char **argv) {
    const char *name = (argc >= 3) ? argv[2] : "leveltest-gate";
    const char *image = "android-stock";   /* seeded at boot, so create() can succeed */

    static char names[CONTAINER_MAX][CONTAINER_NAME_MAX];

    /* Refuse to run if the name is already taken: "already exists" returns -1,
     * which is the same value a refusal returns. */
    int n0 = sys_container_list(names);
    for (int i = 0; i < n0; i++) {
        if (strcmp(names[i], name) == 0) {
            printf("gate: container '%s' is already running -- refusing to test\n", name);
            return 2;
        }
    }

    printf("gate: leveltest --gate as pid %d, trying to create '%s' from '%s'\n",
           sys_getpid(), name, image);
    int rc = sys_container_create(name, image);
    printf("gate: sys_container_create(\"%s\", \"%s\") = %d\n", name, image, rc);

    if (rc >= 0) {
        printf("gate: -> GATE PASSED: got container id %d, so this level may "
               "create containers\n", rc);
        printf("gate: note -- left '%s' behind; remove it with `appvm rm %s`\n",
               name, name);
        return 0;
    }

    printf("gate: -> gate did not pass: call returned %d and no container id\n", rc);
    printf("gate:    %d is ambiguous on its own (-EPERM refusal, or an ordinary\n"
           "gate:    failure such as no slots / unknown image). Confirm against\n"
           "gate:    the `container create` control before calling this a refusal.\n", rc);
    return 0;
}

/* Re-exec ourselves into a container through the container-exec *syscall*.
 *
 * This is the path the shell's `appvm exec` does not take. There the caller
 * is the shell, which has no current_process, so container_exec() creates a
 * fresh process and stamps the level on it. Here we are a real process, so
 * container_exec() reuses our own process_t via proc_exec() instead -- and
 * that is the case where the level has to be actively demoted, or the caller
 * would walk into the container still holding its old level and could then
 * signal host processes from inside.
 *
 * Usage: leveltest --into <container-id> <path> [args...]
 * The re-exec'd copy runs normally and reports what it can and cannot do.
 */
static int enter_container(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: leveltest --into <id> <path> [args...]\n");
        return 2;
    }
    int id = 0;
    for (const char *p = argv[2]; *p; p++) {
        if (*p < '0' || *p > '9') break;
        id = id * 10 + (*p - '0');
    }
    /* sys_container_exec() takes (id, path, argv, argc) with argv[0] the
     * program name, matching the usual exec convention. */
    char *av[32];
    int n = argc - 3;
    if (n > 31) n = 31;
    for (int i = 0; i < n; i++) av[i] = argv[3 + i];
    av[n] = 0;

    printf("leveltest: entering container %d as '%s' (was pid %d)\n",
           id, argv[3], sys_getpid());
    int r = sys_container_exec(id, argv[3], av, n);
    printf("leveltest: container_exec returned %d\n", r);
    return 0;
}

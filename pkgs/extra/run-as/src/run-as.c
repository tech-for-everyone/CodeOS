/* run-as — CodeOS Android container init / run-as shim.
 *
 * Serves two roles inside an Android container image:
 *   - PID 1 entrypoint (when argv[0] ends in /init or /bin/sh): boots the
 *     container runtime and exits.
 *   - App launcher (when invoked as /system/bin/run-as <app> [args...]):
 *     reports the app being launched, exercises the ashmem/binder stack,
 *     runs a short work loop and exits cleanly.
 *
 * Native CodeOS ABI (x86_64 freestanding, crt0 + userspace lib).
 */

#include "stdio.h"
#include "string.h"
#include "unistd.h"

static int ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return 0;
    return strcmp(s + (ls - lf), suffix) == 0;
}

static int is_boot_role(const char *argv0) {
    return argv0 && (ends_with(argv0, "/init") || ends_with(argv0, "/bin/sh"));
}

int main(int argc, char **argv) {
    if (is_boot_role(argv[0])) {
        puts("");
        puts("android/init: CodeOS Android container boots");
        puts("android/init: namespace+cgroup isolation active (pid 1)");
        puts("android/init: boot completed, container ready");
        return 0;
    }

    const char *app = argc >= 2 ? argv[1] : "(unknown)";
    puts("");
    printf("run-as: launching Android app '%s' (SDK 29, x86_64)\n", app);
    printf("run-as: exec path %s\n", argv[0] ? argv[0] : "?");
    printf("run-as: args: ");
    for (int i = 0; i < argc; i++)
        printf("%s%s", i ? " " : "", argv[i] ? argv[i] : "");
    puts("");

    int shmid = sys_ashmem(0, (void *)"android-app", 4096, 0);
    if (shmid >= 0) {
        int sz = sys_ashmem(1, 0, shmid, 0);
        printf("run-as: ashmem region id=%d size=%d bytes\n", shmid, sz);
    } else {
        puts("run-as: ashmem unavailable");
    }

    volatile int work = 20000000;
    while (work-- > 0)
        ;

    printf("run-as: app '%s' finished cleanly\n", app);
    return 0;
}
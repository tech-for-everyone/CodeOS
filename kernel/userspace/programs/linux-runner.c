/* linux-runner — Wine-like translation layer for Linux x86_64 binaries
 * Sets Linux personality on the process, then execve's the target binary.
 * Run: linux-runner /path/to/linux/binary [args...]
 */

#include "unistd.h"
#include "string.h"
#include "stdlib.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        sys_write("usage: linux-runner debian | <linux-binary> [args...]\n", 55);
        sys_exit(1);
    }

    if (strcmp(argv[1], "debian") == 0 ||
        strcmp(argv[1], "--debian") == 0) {
        int container_id = sys_container_create("debian-runner", "debian-minimal");
        if (container_id < 0) {
            sys_write("linux-runner: could not create Debian container\n", 48);
            sys_exit(1);
        }
        /* Start Debian's /sbin/init, falling back to /bin/sh in the container. */
        if (sys_container_start(container_id) < 0) {
            sys_write("linux-runner: could not start Debian container\n", 48);
            sys_container_destroy(container_id);
            sys_exit(1);
        }
        sys_exit(0);
    }

    /* Set Linux personality so kernel translates syscall numbers */
    sys_set_personality(PERSONALITY_LINUX);

    /* Build argv for the target binary (pass through all args after the binary path) */
    char *new_argv[64];
    int nargc = 0;
    for (int i = 1; i < argc && nargc < 63; i++)
        new_argv[nargc++] = argv[i];
    new_argv[nargc] = 0;

    /* execve the Linux binary (kernel will handle shebang + elf loading) */
    sys_execve(new_argv[0], new_argv, nargc);

    /* If execve fails, we land here */
    sys_write("linux-runner: exec failed\n", 27);
    sys_exit(1);
    return 1;
}

/* linux-runner — Wine-like translation layer for Linux x86_64 binaries
 * Sets Linux personality on the process, then execve's the target binary.
 * Run: linux-runner openweb [args...] | debian | /path/to/linux/binary [args...]
 */

#include "unistd.h"
#include "string.h"
#include "stdlib.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        sys_write("usage: linux-runner openweb [args...] | debian | <linux-binary> [args...]\n", 79);
        sys_exit(1);
    }

    if (strcmp(argv[1], "debian") == 0 || strcmp(argv[1], "--debian") == 0) {
        int container_id = sys_container_create("debian-runner", "debian-minimal");
        if (container_id < 0) {
            sys_write("linux-runner: could not create Debian container\n", 48);
            sys_exit(1);
        }
        if (sys_container_start(container_id) < 0) {
            sys_write("linux-runner: could not start Debian container\n", 48);
            sys_container_destroy(container_id);
            sys_exit(1);
        }
        sys_exit(0);
    }

    if (strcmp(argv[1], "openweb") == 0 || strcmp(argv[1], "--openweb") == 0) {
        int container_id = sys_container_create("openweb", "debian-minimal");
        char *openweb_argv[64];
        int nargc = 0;
        if (container_id < 0) {
            sys_write("linux-runner: could not create OpenWeb container\n", 52);
            sys_exit(1);
        }
        /* Do not launch a partially bootstrapped Zen/Firefox tree. The port
         * process must create this marker only after the engine, runtime
         * libraries, graphics bridge, sandbox policy, and OpenWeb branding
         * checks have all passed inside Debian. */
        if (sys_access("/containers/images/debian-minimal/usr/share/openweb/CODEOS_NATIVE_READY", 0) != 0) {
            sys_write("linux-runner: OpenWeb port is not ready; refusing to launch\n", 61);
            sys_container_destroy(container_id);
            sys_exit(1);
        }
        openweb_argv[nargc++] = "/usr/bin/openweb";
        for (int i = 2; i < argc && nargc < 63; i++)
            openweb_argv[nargc++] = argv[i];
        openweb_argv[nargc] = 0;
        sys_container_exec(container_id, "/usr/bin/openweb", openweb_argv, nargc);
        sys_write("linux-runner: OpenWeb is not installed in Debian\n", 49);
        sys_container_destroy(container_id);
        sys_exit(1);
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

/* Chromium Launcher for CodeOS
 * 
 * Launches ungoogled-chromium via the linux-runner compatibility layer.
 * The chromium binary is expected to be in the debian-minimal rootfs.
 */

#include "unistd.h"
#include "string.h"

#define MAX_ARGS 32

int main(int argc, char **argv) {
    sys_write("CodeOS Chromium Launcher\n", 25);
    
    /* Check if linux-runner is available */
    if (sys_access("/bin/linux-runner", 0) != 0) {
        sys_write("Error: linux-runner not found. Install the linux-runner package.\n", 62);
        return 1;
    }
    
    /* Check if debian-minimal rootfs exists */
    if (sys_access("/containers/images/debian-minimal", 0) != 0) {
        sys_write("Error: debian-minimal rootfs not found. Run 'appvm pull debian-minimal' first.\n", 74);
        return 1;
    }
    
    /* Check if chromium is installed in the rootfs */
    if (sys_access("/containers/images/debian-minimal/usr/bin/chromium-browser", 0) != 0 &&
        sys_access("/containers/images/debian-minimal/usr/bin/chromium", 0) != 0) {
        sys_write("Chromium not found in debian-minimal. Installing...\n", 50);
        /* Try to install chromium in the container */
        char *install_argv[] = {
            "/bin/linux-runner",
            "/containers/images/debian-minimal",
            "apt-get", "update", "&&", "apt-get", "install", "-y", "chromium", NULL
        };
        sys_execve("/bin/linux-runner", install_argv, 9);
    }
    
    /* Build arguments for chromium */
    char *chromium_argv[MAX_ARGS];
    int nargc = 0;
    
    /* Use linux-runner to launch chromium in the debian-minimal container */
    chromium_argv[nargc++] = "/bin/linux-runner";
    chromium_argv[nargc++] = "/containers/images/debian-minimal";
    chromium_argv[nargc++] = "/usr/bin/chromium-browser";
    chromium_argv[nargc++] = "--no-sandbox";
    chromium_argv[nargc++] = "--disable-gpu";
    chromium_argv[nargc++] = "--disable-dev-shm-usage";
    chromium_argv[nargc++] = "--no-first-run";
    chromium_argv[nargc++] = "--disable-default-apps";
    
    /* Pass through any additional arguments */
    for (int i = 1; i < argc && nargc < MAX_ARGS - 1; i++) {
        chromium_argv[nargc++] = argv[i];
    }
    chromium_argv[nargc] = NULL;
    
    sys_write("Launching ungoogled-chromium...\n", 32);
    
    /* Execute chromium */
    sys_execve("/bin/linux-runner", chromium_argv, nargc);
    
    sys_write("Failed to launch chromium\n", 26);
    return 1;
}
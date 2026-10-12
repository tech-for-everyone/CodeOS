/* Chromium Launcher for CodeOS
 * 
 * Launches ungoogled-chromium headless with CDP (Chrome DevTools Protocol)
 * and starts the CDP bridge HTTP server in the container.
 * 
 * Architecture:
 *   OpenWeb (kernel) → HTTP :9223 → CDP Bridge (container) → CDP WebSocket :9222 → Chromium
 */

#include "unistd.h"
#include "string.h"
#include "stdlib.h"

#define MAX_ARGS 64

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    sys_write("CodeOS Chromium Engine Launcher\n", 32);
    
    /* Check if linux-runner is available */
    if (sys_access("/bin/linux-runner", 0) != 0) {
        sys_write("Error: linux-runner not found\n", 30);
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
    
    /* Check if CDP bridge is installed */
    if (sys_access("/containers/images/debian-minimal/usr/local/bin/cdp_bridge", 0) != 0) {
        sys_write("CDP bridge not found. Please rebuild with 'make install' in pkgs/core/chromium-launcher\n", 78);
    }
    
    /* Build chromium command with CDP enabled */
    char *chromium_argv[MAX_ARGS];
    int argc_c = 0;
    
    chromium_argv[argc_c++] = "/bin/linux-runner";
    chromium_argv[argc_c++] = "/containers/images/debian-minimal";
    chromium_argv[argc_c++] = "/usr/bin/chromium-browser";
    chromium_argv[argc_c++] = "--headless=new";
    chromium_argv[argc_c++] = "--remote-debugging-port=9222";
    chromium_argv[argc_c++] = "--remote-debugging-address=0.0.0.0";
    chromium_argv[argc_c++] = "--no-sandbox";
    chromium_argv[argc_c++] = "--disable-gpu";
    chromium_argv[argc_c++] = "--disable-dev-shm-usage";
    chromium_argv[argc_c++] = "--no-first-run";
    chromium_argv[argc_c++] = "--disable-default-apps";
    chromium_argv[argc_c++] = "--disable-extensions";
    chromium_argv[argc_c++] = "--disable-background-networking";
    chromium_argv[argc_c++] = "--disable-sync";
    chromium_argv[argc_c++] = "--metrics-recording-only";
    chromium_argv[argc_c++] = "--no-report-upload";
    chromium_argv[argc_c++] = "--window-size=1280,800";
    chromium_argv[argc_c++] = "--force-device-scale-factor=1";
    chromium_argv[argc_c++] = "--hide-scrollbars";
    chromium_argv[argc_c++] = "--mute-audio";
    chromium_argv[argc_c++] = "--blink-settings=imagesEnabled=true";
    chromium_argv[argc_c++] = "--enable-features=NetworkService,NetworkServiceInProcess";
    chromium_argv[argc_c++] = "--disable-features=TranslateUI";
    chromium_argv[argc_c++] = "--disable-ipc-flooding-protection";
    chromium_argv[argc_c++] = "--remote-allow-origins=*";
    chromium_argv[argc_c++] = "about:blank";
    chromium_argv[argc_c] = NULL;
    
    /* Build CDP bridge command */
    char *bridge_argv[] = {
        "/bin/linux-runner",
        "/containers/images/debian-minimal",
        "/usr/local/bin/cdp_bridge",
        NULL
    };
    
    sys_write("Starting Chromium headless with CDP on port 9222...\n", 48);
    
    /* Fork for chromium */
    int chromium_pid = sys_fork();
    if (chromium_pid == 0) {
        /* Child - exec chromium */
        sys_execve("/bin/linux-runner", chromium_argv, argc_c);
        sys_exit(1);
    } else if (chromium_pid < 0) {
        sys_write("Failed to fork chromium\n", 24);
        return 1;
    }
    
    /* Fork for CDP bridge */
    sys_sleep(2000);  /* Wait for chromium to start */
    int bridge_pid = sys_fork();
    if (bridge_pid == 0) {
        /* Child - exec CDP bridge */
        sys_execve("/bin/linux-runner", bridge_argv, 3);
        sys_exit(1);
    } else if (bridge_pid < 0) {
        sys_write("Failed to fork CDP bridge\n", 26);
    }
    
    sys_write("Chromium engine started. CDP on port 9222, Bridge HTTP on port 9223\n", 60);
    sys_write("OpenWeb can now use Chromium as rendering engine via HTTP :9223\n", 56);
    
    /* Wait for children */
    while (1) {
        sys_sleep(1000);
    }
    
    return 0;
}
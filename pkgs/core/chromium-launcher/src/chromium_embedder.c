/* Chromium Embedder for CodeOS OpenWeb
 * 
 * Launches ungoogled-chromium headless with CDP (Chrome DevTools Protocol)
 * and provides a C API for OpenWeb to drive it.
 * 
 * Architecture:
 *   OpenWeb (kernel) → CDP Client (Rust in ow_http) → Chromium (container, port 9222)
 * 
 * Chromium runs: chromium-browser --headless --remote-debugging-port=9222 --no-sandbox ...
 */

#include "unistd.h"
#include "string.h"
#include "stdlib.h"

#define MAX_ARGS 64
#define CDP_PORT 9222

/* Chromium process management */
static int chromium_pid = -1;
static int chromium_running = 0;

/* Start chromium headless with CDP in debian-minimal container */
int chromium_start(void) {
    if (chromium_running) return 0;
    
    sys_write("Starting Chromium headless with CDP...\n", 39);
    
    /* Check dependencies */
    if (sys_access("/bin/linux-runner", 0) != 0) {
        sys_write("Error: linux-runner not found\n", 30);
        return -1;
    }
    if (sys_access("/containers/images/debian-minimal", 0) != 0) {
        sys_write("Error: debian-minimal rootfs not found\n", 39);
        return -1;
    }
    
    /* Build chromium command with CDP enabled */
    char *argv[MAX_ARGS];
    int argc = 0;
    
    argv[argc++] = "/bin/linux-runner";
    argv[argc++] = "/containers/images/debian-minimal";
    argv[argc++] = "/usr/bin/chromium-browser";
    argv[argc++] = "--headless=new";
    argv[argc++] = "--remote-debugging-port=9222";
    argv[argc++] = "--remote-debugging-address=0.0.0.0";
    argv[argc++] = "--no-sandbox";
    argv[argc++] = "--disable-gpu";
    argv[argc++] = "--disable-dev-shm-usage";
    argv[argc++] = "--no-first-run";
    argv[argc++] = "--disable-default-apps";
    argv[argc++] = "--disable-extensions";
    argv[argc++] = "--disable-background-networking";
    argv[argc++] = "--disable-sync";
    argv[argc++] = "--metrics-recording-only";
    argv[argc++] = "--no-report-upload";
    argv[argc++] = "--window-size=1280,800";
    argv[argc++] = "--force-device-scale-factor=1";
    argv[argc++] = "--hide-scrollbars";
    argv[argc++] = "--mute-audio";
    argv[argc++] = "--blink-settings=imagesEnabled=true";
    argv[argc++] = "--enable-features=NetworkService,NetworkServiceInProcess";
    argv[argc++] = "--disable-features=TranslateUI";
    argv[argc++] = "--disable-ipc-flooding-protection";
    argv[argc++] = "--remote-allow-origins=*";
    argv[argc++] = "about:blank";
    argv[argc] = NULL;
    
    /* Fork and exec */
    chromium_pid = sys_fork();
    if (chromium_pid == 0) {
        /* Child - exec chromium */
        sys_execve("/bin/linux-runner", argv, argc);
        sys_exit(1);
    } else if (chromium_pid < 0) {
        sys_write("Failed to fork chromium\n", 24);
        return -1;
    }
    
    /* Parent - wait for CDP to be ready */
    chromium_running = 1;
    
    /* Wait for CDP port to be available (poll with sleep) */
    for (int i = 0; i < 50; i++) {
        sys_sleep_ms(200);
        /* Try to connect to CDP port */
        int sock = sys_socket(2, 1, 0);  /* AF_INET, SOCK_STREAM */
        if (sock >= 0) {
            struct sockaddr_in addr;
            addr.sin_family = 2;
            addr.sin_port = __builtin_bswap16(CDP_PORT);
            addr.sin_addr.s_addr = __builtin_bswap32(0x7F000001);  /* 127.0.0.1 */
            if (sys_connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                sys_close(sock);
                sys_write("Chromium CDP ready on port 9222\n", 34);
                return 0;
            }
            sys_close(sock);
        }
    }
    
    sys_write("Warning: Chromium CDP not ready after 10s\n", 40);
    return 0;  /* Continue anyway */
}

/* Stop chromium */
void chromium_stop(void) {
    if (!chromium_running) return;
    
    sys_write("Stopping Chromium...\n", 21);
    if (chromium_pid > 0) {
        sys_kill(chromium_pid, 9);  /* SIGKILL */
        chromium_pid = -1;
    }
    chromium_running = 0;
}

/* Check if chromium is running */
int chromium_is_running(void) {
    return chromium_running;
}

/* CDP command structure */
struct cdp_command {
    int id;
    const char *method;
    const char *params_json;
};

/* CDP response structure */
struct cdp_response {
    int id;
    int success;
    char *result_json;
    char *error_json;
};

/* Send CDP command over WebSocket (simplified - uses raw socket + HTTP upgrade) */
/* In practice, this would use a proper WebSocket implementation */
int chromium_cdp_send(const char *method, const char *params, char *response_buf, int response_max) {
    /* Simplified: connect to CDP WebSocket and send command */
    /* For now, return a mock response - real impl needs WebSocket framing */
    int sock = sys_socket(2, 1, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr;
    addr.sin_family = 2;
    addr.sin_port = __builtin_bswap16(CDP_PORT);
    addr.sin_addr.s_addr = __builtin_bswap32(0x7F000001);
    
    if (sys_connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sys_close(sock);
        return -1;
    }
    
    /* HTTP upgrade request for WebSocket */
    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET /devtools/browser HTTP/1.1\r\n"
        "Host: localhost:9222\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n");
    
    sys_write(sock, req, req_len);
    
    /* Read response (skip HTTP headers) */
    char buf[1024];
    int n = sys_read(sock, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        /* Look for WebSocket accept */
        if (strstr(buf, "101 Switching Protocols")) {
            /* Send CDP command over WebSocket (simplified - no framing) */
            char cmd[1024];
            static int cmd_id = 1;
            int cmd_len = snprintf(cmd, sizeof(cmd),
                "{\"id\":%d,\"method\":\"%s\",\"params\":%s}", 
                cmd_id++, method, params ? params : "{}");
            
            /* WebSocket frame: FIN(1) + OPCODE(1=text) + MASK(0) + PAYLOAD_LEN + PAYLOAD */
            char frame[1040];
            frame[0] = 0x81;  /* FIN + TEXT */
            if (cmd_len < 126) {
                frame[1] = cmd_len;
                memcpy(frame + 2, cmd, cmd_len);
                sys_write(sock, frame, cmd_len + 2);
            }
            
            /* Read response */
            n = sys_read(sock, response_buf, response_max - 1);
            if (n > 0) {
                response_buf[n] = 0;
                sys_close(sock);
                return 0;
            }
        }
    }
    
    sys_close(sock);
    return -1;
}

/* Navigate to URL */
int chromium_navigate(const char *url) {
    char params[512];
    snprintf(params, sizeof(params), "{\"url\":\"%s\"}", url);
    char response[4096];
    return chromium_cdp_send("Page.navigate", params, response, sizeof(response));
}

/* Capture screenshot as PNG */
int chromium_screenshot(char *png_buf, int *png_len, int max_len) {
    char params[256];
    snprintf(params, sizeof(params), "{\"format\":\"png\",\"fromSurface\":true}");
    char response[8192];
    int r = chromium_cdp_send("Page.captureScreenshot", params, response, sizeof(response));
    if (r == 0) {
        /* Parse base64 PNG from response */
        /* For now, just return success */
        *png_len = 0;
    }
    return r;
}

/* Get page content (DOM) */
int chromium_get_content(char *content_buf, int max_len) {
    char params[128];
    snprintf(params, sizeof(params), "{}");
    char response[8192];
    return chromium_cdp_send("Runtime.evaluate", params, response, sizeof(response));
}

/* Execute JavaScript */
int chromium_eval_js(const char *script, char *result_buf, int max_len) {
    char params[1024];
    snprintf(params, sizeof(params), "{\"expression\":\"%s\",\"awaitPromise\":true}", script);
    char response[4096];
    return chromium_cdp_send("Runtime.evaluate", params, response, sizeof(response));
}

/* Initialize CDP domains needed */
int chromium_cdp_init(void) {
    char response[4096];
    chromium_cdp_send("Target.setAutoAttach", "{\"autoAttach\":true,\"waitForDebuggerOnStart\":false,\"flatten\":true}", response, sizeof(response));
    chromium_cdp_send("Page.enable", "{}", response, sizeof(response));
    chromium_cdp_send("Runtime.enable", "{}", response, sizeof(response));
    chromium_cdp_send("Network.enable", "{}", response, sizeof(response));
    chromium_cdp_send("DOM.enable", "{}", response, sizeof(response));
    return 0;
}

/* Public API */
int chromium_embedder_init(void) {
    int r = chromium_start();
    if (r == 0) {
        chromium_cdp_init();
    }
    return r;
}

void chromium_embedder_shutdown(void) {
    chromium_stop();
}
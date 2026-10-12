/* CDP Bridge for CodeOS OpenWeb
 * 
 * Minimal HTTP server that bridges HTTP requests to Chromium CDP WebSocket.
 * Compiled for the debian-minimal container (x86_64 Linux).
 * 
 * Build: x86_64-linux-gnu-gcc -o cdp_bridge cdp_bridge.c -lpthread
 * Run inside container: ./cdp_bridge
 * 
 * OpenWeb (kernel) → HTTP :9223 → this bridge → CDP WebSocket :9222 → Chromium
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

#define HTTP_PORT 9223
#define CDP_PORT 9222
#define MAX_EVENTS 64
#define BUF_SIZE 8192

static int http_sock = -1;
static int cdp_sock = -1;
static int running = 1;

/* Simple JSON helpers */
static char *json_escape(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len * 2 + 1);
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        switch (s[i]) {
            case '"': *p++ = '\\'; *p++ = '"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b'; break;
            case '\f': *p++ = '\\'; *p++ = 'f'; break;
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            default: *p++ = s[i]; break;
        }
    }
    *p = 0;
    return out;
}

static char *json_get(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        char *end = strchr(p, '"');
        if (end) {
            size_t len = end - p;
            char *val = malloc(len + 1);
            memcpy(val, p, len);
            val[len] = 0;
            return val;
        }
    } else if (*p == '{' || *p == '[' || *p == 't' || *p == 'f' || *p == 'n' || (*p >= '0' && *p <= '9') || *p == '-') {
        char *end = p;
        int depth = 0;
        while (*end) {
            if (*end == '{' || *end == '[') depth++;
            else if (*end == '}' || *end == ']') depth--;
            else if (*end == ',' && depth == 0) break;
            end++;
        }
        size_t len = end - p;
        char *val = malloc(len + 1);
        memcpy(val, p, len);
        val[len] = 0;
        return val;
    }
    return NULL;
}

/* WebSocket frame helpers */
static int ws_frame_text(char *out, const char *payload) {
    size_t len = strlen(payload);
    out[0] = 0x81;  /* FIN + TEXT */
    if (len < 126) {
        out[1] = len;
        memcpy(out + 2, payload, len);
        return len + 2;
    } else if (len < 65536) {
        out[1] = 126;
        out[2] = (len >> 8) & 0xFF;
        out[3] = len & 0xFF;
        memcpy(out + 4, payload, len);
        return len + 4;
    }
    return -1;
}

static int ws_parse_frame(const char *data, size_t len, char **out_payload, size_t *out_len) {
    if (len < 2) return 0;
    unsigned char opcode = data[0] & 0x0F;
    unsigned char masked = data[1] & 0x80;
    size_t payload_len = data[1] & 0x7F;
    size_t offset = 2;
    
    if (payload_len == 126) {
        if (len < 4) return 0;
        payload_len = (data[2] << 8) | data[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (len < 10) return 0;
        offset = 10;
    }
    
    if (masked) {
        if (len < offset + 4) return 0;
        offset += 4;  /* skip mask key */
    }
    
    if (len < offset + payload_len) return 0;
    
    *out_payload = (char*)(data + offset);
    *out_len = payload_len;
    return 1;
}

/* Send JSON-RPC over WebSocket */
static int cdp_send(int sock, int id, const char *method, const char *params) {
    char msg[4096];
    int len = snprintf(msg, sizeof(msg), "{\"id\":%d,\"method\":\"%s\",\"params\":%s}", 
        id, method, params ? params : "{}");
    
    char frame[4100];
    int flen = ws_frame_text((char*)msg, msg);
    if (flen < 0) return -1;
    
    return send(sock, msg, flen, 0);
}

/* Read and parse WebSocket frame */
static int cdp_recv(int sock, char *buf, size_t max) {
    int n = recv(sock, buf, max - 1, 0);
    if (n <= 0) return n;
    buf[n] = 0;
    return n;
}

/* Connect to CDP WebSocket */
static int connect_cdp() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    /* HTTP Upgrade request */
    char req[512];
    int len = snprintf(req, sizeof(req),
        "GET /devtools/browser HTTP/1.1\r\n"
        "Host: localhost:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n", CDP_PORT);
    
    send(sock, req, strlen(req), 0);
    
    /* Read HTTP response */
    char buf[1024];
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(sock); return -1; }
    buf[n] = 0;
    
    if (!strstr(buf, "101 Switching Protocols")) {
        close(sock);
        return -1;
    }
    
    return sock;
}

/* Send CDP command and wait for response */
static int cdp_cmd(int sock, int id, const char *method, const char *params, char *response, size_t max) {
    if (cdp_send(sock, id, method, params) < 0) return -1;
    
    char buf[8192];
    while (1) {
        int n = cdp_recv(sock, buf, sizeof(buf));
        if (n <= 0) return -1;
        
        /* Parse WebSocket frame */
        char *payload;
        size_t plen;
        if (!ws_parse_frame(buf, n, &payload, &plen)) continue;
        
        /* Null-terminate payload for parsing */
        char save = payload[plen];
        payload[plen] = 0;
        
        /* Check if this is our response */
        char *id_str = json_get(payload, "id");
        if (id_str && atoi(id_str) == id) {
            if (response && max > 0) {
                strncpy(response, payload, max - 1);
                response[max - 1] = 0;
            }
            free(id_str);
            return 0;
        }
        free(id_str);
    }
}

/* Initialize CDP domains */
static int cdp_init(int sock) {
    char resp[4096];
    cdp_cmd(sock, 1, "Target.setAutoAttach", "{\"autoAttach\":true,\"waitForDebuggerOnStart\":false,\"flatten\":true}", resp, sizeof(resp));
    cdp_cmd(sock, 2, "Page.enable", "{}", resp, sizeof(resp));
    cdp_cmd(sock, 3, "Runtime.enable", "{}", resp, sizeof(resp));
    cdp_cmd(sock, 4, "Network.enable", "{}", resp, sizeof(resp));
    cdp_cmd(sock, 5, "DOM.enable", "{}", resp, sizeof(resp));
    cdp_cmd(sock, 6, "Target.setAutoAttach", "{\"autoAttach\":true,\"waitForDebuggerOnStart\":false,\"flatten\":true}", resp, sizeof(resp));
    return 0;
}

/* Handle HTTP request */
static void handle_request(int client_sock) {
    char buf[BUF_SIZE];
    int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(client_sock); return; }
    buf[n] = 0;
    
    /* Parse HTTP request */
    char *method = strtok(buf, " ");
    char *path = strtok(NULL, " ");
    if (!method || !path) { close(client_sock); return; }
    
    /* Find body */
    char *body = strstr(buf, "\r\n\r\n");
    if (body) body += 4;
    else body = "";
    
    /* Parse JSON params */
    char *params = NULL;
    if (strcmp(method, "POST") == 0) {
        params = strstr(body, "{");
    }
    
    char response[8192];
    int status = 200;
    const char *content_type = "application/json";
    char *response_body = "{}";
    
    if (strcmp(path, "/navigate") == 0) {
        char *url = json_get(params ? params : "{}", "url");
        if (url) {
            static int cmd_id = 100;
            char resp[4096];
            char p[512];
            snprintf(p, sizeof(p), "{\"url\":\"%s\"}", url);
            if (cdp_cmd(cdp_sock, cmd_id++, "Page.navigate", p, resp, sizeof(resp)) == 0) {
                response_body = resp;
            } else {
                response_body = "{\"error\":\"CDP command failed\"}";
            }
            free(url);
        } else {
            response_body = "{\"error\":\"Missing url parameter\"}";
        }
    } else if (strcmp(path, "/screenshot") == 0) {
        static int cmd_id = 200;
        char resp[16384];
        if (cdp_cmd(cdp_sock, cmd_id++, "Page.captureScreenshot", "{\"format\":\"png\",\"fromSurface\":true,\"captureBeyondViewport\":true}", resp, sizeof(resp)) == 0) {
            char *data = json_get(resp, "data");
            if (data) {
                char *escaped = json_escape(data);
                int len = snprintf(response, sizeof(response), "{\"png_base64\":\"%s\"}", escaped);
                response_body = response;
                free(escaped);
            } else {
                response_body = resp;
            }
            free(data);
        } else {
            response_body = "{\"error\":\"CDP command failed\"}";
        }
    } else if (strcmp(path, "/content") == 0) {
        static int cmd_id = 300;
        char resp[16384];
        if (cdp_cmd(cdp_sock, cmd_id++, "Runtime.evaluate", "{\"expression\":\"document.documentElement.outerHTML\",\"awaitPromise\":true}", resp, sizeof(resp)) == 0) {
            char *result = json_get(resp, "result");
            if (result) {
                char *value = json_get(result, "value");
                if (value) {
                    char *escaped = json_escape(value);
                    int len = snprintf(response, sizeof(response), "{\"html\":\"%s\"}", escaped);
                    response_body = response;
                    free(escaped);
                }
                free(value);
            }
            free(result);
        } else {
            response_body = "{\"error\":\"CDP command failed\"}";
        }
    } else if (strcmp(path, "/eval") == 0) {
        char *expr = json_get(params ? params : "{}", "expression");
        if (expr) {
            static int cmd_id = 400;
            char resp[8192];
            char p[2048];
            snprintf(p, sizeof(p), "{\"expression\":\"%s\",\"awaitPromise\":true}", expr);
            if (cdp_cmd(cdp_sock, cmd_id++, "Runtime.evaluate", p, resp, sizeof(resp)) == 0) {
                response_body = resp;
            } else {
                response_body = "{\"error\":\"CDP command failed\"}";
            }
            free(expr);
        } else {
            response_body = "{\"error\":\"Missing expression parameter\"}";
        }
    } else if (strcmp(path, "/navigate_back") == 0) {
        static int cmd_id = 500;
        char resp[4096];
        cdp_cmd(cdp_sock, cmd_id++, "Page.goBack", "{}", resp, sizeof(resp));
        response_body = resp;
    } else if (strcmp(path, "/navigate_forward") == 0) {
        static int cmd_id = 600;
        char resp[4096];
        cdp_cmd(cdp_sock, cmd_id++, "Page.goForward", "{}", resp, sizeof(resp));
        response_body = resp;
    } else if (strcmp(path, "/reload") == 0) {
        static int cmd_id = 700;
        char resp[4096];
        cdp_cmd(cdp_sock, cmd_id++, "Page.reload", "{}", resp, sizeof(resp));
        response_body = resp;
    } else if (strcmp(path, "/stop") == 0) {
        static int cmd_id = 800;
        char resp[4096];
        cdp_cmd(cdp_sock, cmd_id++, "Page.stopLoading", "{}", resp, sizeof(resp));
        response_body = resp;
    } else if (strcmp(path, "/status") == 0) {
        response_body = "{\"connected\":true}";
    } else if (strcmp(path, "/health") == 0) {
        response_body = "{\"status\":\"ok\"}";
    } else {
        status = 404;
        response_body = "{\"error\":\"Not found\"}";
    }
    
    /* Send HTTP response */
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n", status, content_type, strlen(response_body));
    
    send(client_sock, header, hlen, 0);
    send(client_sock, response_body, strlen(response_body), 0);
    close(client_sock);
}

/* Signal handler */
static void sig_handler(int sig) {
    running = 0;
    if (http_sock >= 0) close(http_sock);
    if (cdp_sock >= 0) close(cdp_sock);
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    /* Connect to CDP */
    printf("Connecting to Chromium CDP on port %d...\n", CDP_PORT);
    cdp_sock = connect_cdp();
    if (cdp_sock < 0) {
        fprintf(stderr, "Failed to connect to CDP. Is Chromium running with --remote-debugging-port=%d?\n", CDP_PORT);
        return 1;
    }
    printf("Connected to CDP\n");
    
    cdp_init(cdp_sock);
    printf("CDP initialized\n");
    
    /* Create HTTP server socket */
    http_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (http_sock < 0) { perror("socket"); return 1; }
    
    int opt = 1;
    setsockopt(http_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(http_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    
    if (listen(http_sock, 10) < 0) { perror("listen"); return 1; }
    
    printf("CDP Bridge HTTP server listening on port %d\n", HTTP_PORT);
    
    /* Main loop */
    struct epoll_event ev, events[MAX_EVENTS];
    int epfd = epoll_create1(0);
    ev.events = EPOLLIN;
    ev.data.fd = http_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, http_sock, &ev);
    
    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == http_sock) {
                struct sockaddr_in cli_addr;
                socklen_t cli_len = sizeof(cli_addr);
                int client = accept(http_sock, (struct sockaddr*)&cli_addr, &cli_len);
                if (client >= 0) {
                    handle_request(client);
                }
            }
        }
    }
    
    close(http_sock);
    if (cdp_sock >= 0) close(cdp_sock);
    return 0;
}
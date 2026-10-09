/* url.c — HTTP URL parser */

#include "url.h"
#include "string.h"

int url_parse(const char *url_str, url_t *out) {
    if (!url_str || !out) return -1;
    memset(out, 0, sizeof(*out));

    const char *p = url_str;

    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else {
        return -1;
    }

    const char *host_start = p;
    const char *host_end = NULL;

    for (; *p; p++) {
        if (*p == '/' || *p == '#' || *p == '?') {
            host_end = p;
            break;
        }
    }
    if (!host_end) host_end = p;

    /* Parse host:port */
    const char *colon = NULL;
    for (const char *q = host_start; q < host_end; q++) {
        if (*q == ':') { colon = q; break; }
    }

    int host_len = colon ? (int)(colon - host_start) : (int)(host_end - host_start);
    if (host_len <= 0 || host_len >= (int)sizeof(out->host)) return -1;

    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    if (colon) {
        uint32_t port = 0;
        for (const char *q = colon + 1; q < host_end; q++) {
            if (*q < '0' || *q > '9') return -1;
            port = port * 10 + (*q - '0');
            if (port > 65535) return -1;
        }
        out->port = port;
    } else {
        out->port = 80;
    }

    /* Path */
    if (*host_end == '/') {
        int path_len = (int)strlen(host_end);
        if (path_len >= (int)sizeof(out->path)) path_len = (int)sizeof(out->path) - 1;
        memcpy(out->path, host_end, path_len);
        out->path[path_len] = '\0';
    } else {
        out->path[0] = '/';
        out->path[1] = '\0';
    }

    return 0;
}

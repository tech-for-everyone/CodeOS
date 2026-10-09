/* url.h — HTTP URL parser */

#ifndef URL_H
#define URL_H

#include "types.h"

typedef struct {
    char     host[256];
    uint16_t port;
    char     path[512];
} url_t;

/* Parse an HTTP URL. Returns 0 on success, -1 on error.
 * Populates host, port, and path. */
int url_parse(const char *url_str, url_t *out);

#endif

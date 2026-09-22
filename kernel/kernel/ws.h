#ifndef WS_H
#define WS_H

#include <stdint.h>
#include <stddef.h>
#include "net.h"

#define WS_MAX_FRAME 4096

#define WS_OP_CONT   0x0
#define WS_OP_TEXT   0x1
#define WS_OP_BINARY 0x2
#define WS_OP_CLOSE  0x8
#define WS_OP_PING   0x9
#define WS_OP_PONG   0xA

typedef struct {
    int fin;
    uint8_t opcode;
    int masked;
    uint8_t mask[4];
    const unsigned char *payload;
    size_t payload_len;
} ws_frame_t;

/* Encode a client (masked) or server (unmasked) frame.
 * Returns length written, or -1 if out is too small. */
int ws_make_frame(unsigned char *out, size_t out_len, const void *data, size_t len, uint8_t opcode, int fin, int mask);

/* Parse a frame from `in`; payload points into `in` (still masked).
 * Returns total bytes consumed (>0) or -1 on short input. */
int ws_parse_frame(const unsigned char *in, size_t len, ws_frame_t *frame);

/* Server-side accept key derivation (RFC 6455 §4.2.2). */
int ws_gen_accept(const char *key, char *out, size_t out_len);

/* Perform the client upgrade handshake over an already-connected transport
 * (w->send/w->recv must be wired by the caller). Returns 0 on success. */
int ws_client_handshake(ws_client_t *w, const char *host, const char *path);

/* Build the server 101 response for a raw HTTP upgrade request. */
int ws_server_handshake(const char *request, char *response, size_t resp_len);

#endif
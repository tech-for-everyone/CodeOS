#include "ws.h"
#include "kprintf.h"
#include "string.h"
#include "timer.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

static const char *ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int ws_gen_accept(const char *key, char *out, size_t out_len) {
    char combined[128];
    int n = sprintf(combined, "%s%s", key, ws_guid);
    unsigned char hash[20];
    mbedtls_sha1((const unsigned char *)combined, (size_t)n, hash);
    size_t olen;
    if (mbedtls_base64_encode((unsigned char *)out, out_len, &olen, hash, 20) != 0)
        return -1;
    out[olen] = 0;
    return 0;
}

int ws_make_frame(unsigned char *out, size_t out_len, const void *data, size_t len, uint8_t opcode, int fin, int mask) {
    size_t need = 2;
    if (len < 126)      need += 0;
    else if (len < 65536) need += 2;
    else                  need += 8;
    if (mask) need += 4;
    need += len;
    if (out_len < need) return -1;

    unsigned char *p = out;
    *p++ = (fin ? 0x80 : 0x00) | (opcode & 0x0F);

    if (len < 126) {
        *p++ = (mask ? 0x80 : 0x00) | (len & 0x7F);
    } else if (len < 65536) {
        *p++ = (mask ? 0x80 : 0x00) | 126;
        *p++ = (len >> 8) & 0xFF;
        *p++ = len & 0xFF;
    } else {
        *p++ = (mask ? 0x80 : 0x00) | 127;
        for (int i = 7; i >= 0; i--) *p++ = (len >> (i * 8)) & 0xFF;
    }

    uint32_t mask_key = 0;
    if (mask) {
        mask_key = (uint32_t)timer_get_milliseconds();
        *p++ = mask_key >> 24;
        *p++ = (mask_key >> 16) & 0xFF;
        *p++ = (mask_key >> 8) & 0xFF;
        *p++ = mask_key & 0xFF;
    }

    const unsigned char *src = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        unsigned char b = src[i];
        if (mask) b ^= (mask_key >> (24 - (i % 4) * 8)) & 0xFF;
        *p++ = b;
    }

    return (int)(p - out);
}

int ws_parse_frame(const unsigned char *in, size_t len, ws_frame_t *frame) {
    if (len < 2) return -1;

    frame->fin = in[0] & 0x80;
    frame->opcode = in[0] & 0x0F;
    frame->masked = in[1] & 0x80;
    size_t payload_len = in[1] & 0x7F;
    size_t pos = 2;

    if (payload_len == 126) {
        if (len < 4) return -1;
        payload_len = ((size_t)in[2] << 8) | in[3];
        pos = 4;
    } else if (payload_len == 127) {
        if (len < 10) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | in[2 + i];
        pos = 10;
    }

    if (frame->masked) {
        if (len < pos + 4) return -1;
        frame->mask[0] = in[pos++];
        frame->mask[1] = in[pos++];
        frame->mask[2] = in[pos++];
        frame->mask[3] = in[pos++];
    }

    if (len < pos + payload_len) return -1;

    frame->payload = in + pos;
    frame->payload_len = payload_len;
    return (int)(pos + payload_len);
}

int ws_server_handshake(const char *request, char *response, size_t resp_len) {
    const char *key_start = strstr(request, "Sec-WebSocket-Key:");
    if (!key_start) return -1;
    key_start += 18;
    while (*key_start == ' ' || *key_start == '\t') key_start++;
    char key[64];
    int i = 0;
    while (*key_start && *key_start != '\r' && *key_start != '\n' && i < 63)
        key[i++] = *key_start++;
    key[i] = 0;

    char accept[64];
    if (ws_gen_accept(key, accept, sizeof(accept)) < 0) return -1;

    return snprintf(response, resp_len,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
}

int ws_client_handshake(ws_client_t *w, const char *host, const char *path) {
    char key[64];
    uint32_t t = (uint32_t)timer_get_milliseconds();
    for (int i = 0; i < 16; i++) {
        uint8_t b = (uint8_t)(t >> ((i % 4) * 8));
        key[i * 2]     = "0123456789abcdef"[(b >> 4) & 0xF];
        key[i * 2 + 1] = "0123456789abcdef"[b & 0xF];
        t ^= (uint32_t)(t * 2654435761u);
    }
    key[32] = 0;

    char req[512];
    int n = sprintf(req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", path, host, key);

    if (w->send(w->ctx, req, n) != n) return -1;

    char resp[1024];
    int r = w->recv(w->ctx, resp, sizeof(resp) - 1, 5000);
    if (r <= 0) return -1;
    resp[r] = 0;

    if (!strstr(resp, "101 Switching Protocols")) return -1;
    if (!strstr(resp, "Upgrade: websocket")) return -1;
    if (!strstr(resp, "Sec-WebSocket-Accept:")) return -1;

    w->state = WS_CONN_OPEN;
    return 0;
}
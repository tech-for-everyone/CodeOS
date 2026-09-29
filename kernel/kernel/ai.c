/* Ziggy AI — HTTP transport shim.
 *
 * The AI no longer lives in the kernel. The brain is now a Python backend
 * running on the dev host (Ziggy repo: backend/ — stdlib-only
 * ThreadingHTTPServer). This file is just the plumbing: it POSTs the
 * prompt to the host loopback through the QEMU user-net gateway
 * (10.0.2.2 -> host loopback) at ZIGGY_AI_PORT, and hands the reply to
 * whoever called ai_query() (SYSCALL_AI_QUERY -> the Qt renderer).
 *
 * The Python engine is an order-faithful port of the old in-kernel C
 * ELIZA (same interleaved AND/OR rule order and boundary matching), so
 * the answers behave exactly like the brain that used to live here.
 *
 * Protocol (see backend/server.py): POST /query, body = prompt; reply
 * body is the plain-text answer. A 204/empty body means "no answer"
 * (the renderer shows its fallback), and the literal __CLEAR__ reply
 * tells the renderer to reset the conversation.
 */

#include "ai.h"
#include "string.h"
#include "kprintf.h"

/* net.c public HTTP wrapper — same extern pattern as xora.c's http_get. */
extern int http_post(const char *host, uint16_t port, const char *path,
                     const void *body, uint16_t body_len,
                     void *buf, uint16_t max_len);

int ai_query(const char *prompt, char *response, int max_len) {
    if (!prompt || !response || max_len <= 0) return -1;

    uint16_t len = 0;
    while (len < 511 && prompt[len]) len++;
    if (len == 0) return -1;

    /* Keep one byte of headroom so the reply is always NUL-terminated
     * (http_exchange copies raw body bytes, with no terminator). */
    if (max_len > 65535) max_len = 65535;
    uint16_t room = (uint16_t)(max_len - 1);

    int n = http_post(ZIGGY_AI_HOST, ZIGGY_AI_PORT, "/query",
                      prompt, len, response, room);
    if (n < 0) {
        kprintf("Ziggy: backend unreachable (%s:%d) — run "
                "backend/server.py on the host\n", ZIGGY_AI_HOST,
                ZIGGY_AI_PORT);
        return -1;
    }
    response[n] = 0;
    return n;
}
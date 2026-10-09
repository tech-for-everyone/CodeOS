/* ─────────────────────────────────────────────────────────────
 * ZircApp → CodeOS platform glue.
 *
 * Zircon's zircapp/zircapp.c is written against Zircon's userspace
 * stubs.c: a small set of zs_* wrappers over the native `int $0x80`
 * ABI, plus usleep().  CodeOS userspace exposes the same syscalls but
 * under different names (kernel/userspace/include/unistd.h), and the
 * two headers redefine the SYSCALL_* macros, so this file speaks the
 * ABI directly and includes only the Zircon ABI header for its types.
 *
 * Nothing here opens a window or touches the WM bridge: the host's own
 * main() (pkgs/core/zircapp/src/zircapp.c) owns that.  These shims exist
 * so the unmodified framework object links.
 * ───────────────────────────────────────────────────────────── */
#include "zircon_abi.h"

static inline long zc_syscall1(long n, long a1) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a1) : "memory");
    return r;
}

static inline long zc_syscall2(long n, long a1, long a2) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "memory");
    return r;
}

/* freestd unistd.h has no usleep; the kernel SLEEP syscall is in ms. */
int usleep(unsigned int usec) {
    zc_syscall1(SYSCALL_SLEEP, (long)((usec + 999u) / 1000u));
    return 0;
}

/* Query the shared Zircon IPC ring: physical address, or -1 when the
 * kernel did not create one (the normal case for a CodeOS process). */
long zs_ipc_query(void) {
    return zc_syscall2(SYSCALL_ZIRCON_IPC, ZIRCON_IPC_QUERY, 0);
}

int zs_ipc_recv(zircon_ipc_msg_t *msg) {
    return (int)zc_syscall2(SYSCALL_ZIRCON_IPC, ZIRCON_IPC_RECV, (long)msg);
}

int zs_ipc_send(const zircon_ipc_msg_t *msg) {
    return (int)zc_syscall2(SYSCALL_ZIRCON_IPC, ZIRCON_IPC_SEND, (long)msg);
}

int zs_input_poll(zircon_input_ev_t *ev) {
    return (int)zc_syscall1(SYSCALL_INPUT_POLL, (long)ev);
}

int zs_fb_info(zircon_fb_info_t *info) {
    return (int)zc_syscall1(SYSCALL_FB_INFO, (long)info);
}

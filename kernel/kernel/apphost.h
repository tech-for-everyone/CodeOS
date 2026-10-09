#ifndef APPHOST_H
#define APPHOST_H

#include "types.h"

/* Launch a userspace ELF on a dedicated scheduler thread (ring 3). The
 * caller (Qt desktop) stays a kernel thread and can keep draining the
 * captured stdout / injecting stdin while the app runs. Only one hosted
 * app may run at a time. Returns 0 on success, -1 on failure. */
int  apphost_launch(const char *path);

/* Launch a userspace ELF on a dedicated scheduler thread from inside a
 * container's namespaces (mount/pid/net/user/uts + cgroup), capturing its
 * stdout/stdin through the apphost rings. Used by the Qt desktop to run
 * Android apps asynchronously so the compositor keeps drawing. Returns 0 on
 * success, -1 on failure or when a hosted app is already running. */
int  apphost_launch_container(int container_id, const char *path);

int  apphost_active(void);
int  apphost_exited(void);
int  apphost_exit_status(void);

/* stdout capture (written from syscall WRITE, drained by the host window) */
int  apphost_write_out(const uint8_t *buf, int len);
int  apphost_drain(uint8_t *buf, int max);

/* stdin injection (written by the host window, read by syscall READ fd 0).
 * apphost_kill() queues a "q\n" quit request and makes the next reads return
 * a single 0xFF marker, then EOF (0) instead of blocking, so the host can
 * tear the app down without it deadlocking on stdin. */
int  apphost_write_in(const uint8_t *buf, int len);
int  apphost_read_in(uint8_t *buf, int max);
void apphost_kill(void);
int  apphost_killed(void);

void apphost_clear(void);

#define APPHOST_QUIT_BYTE 0xFF

#endif

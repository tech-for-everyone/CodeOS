#ifndef USER_WM_H
#define USER_WM_H

#include "types.h"

/* User-window bridge: gives a userspace process a virtual window channel
 * on fds 3 (commands) and 4 (events) speaking the WM protocol
 * (kernel/userspace/include/wm_protocol.h).  The bridge renders into LVGL
 * desktop windows (lvgl_wm placement + lv_canvas surfaces) so Android apps
 * surface real windows on the Qt desktop.  When no compositor is running
 * the CREATE just fails and the app falls back to console mode, keeping
 * headless verification deterministic. */

#define UW_MAX_CLIENTS 8
#define UW_MAX_WINDOWS 16
#define UW_EV_RING     64
#define UW_MAX_TITLE   64

/* User-window fds (match WM_PIPE_* in wm_protocol.h). */
#define UW_FD_CMD  3
#define UW_FD_EVT  4

void user_wm_init(void);
/* 1 once user_wm_init() has armed the bridge (independent of whether the
 * compositor is actually ticking -- apps self-report console fallback). */
int  user_wm_ready(void);

/* Register a process (by pid) as a window client. Idempotent per pid. */
int  user_wm_setup(int pid);
/* Drop a client: queues CLOSED on its windows and frees their surfaces. */
int  user_wm_release(int pid);
int  user_wm_active(int pid);

/* Syscall-side entry points (copy the userspace buffer first). */
int  user_wm_msg_in(int pid, const void *buf, int len);
int  user_wm_events_out(int pid, void *buf, int max);

/* Compositor-side entry points (called from the LVGL input read callbacks,
 * i.e. on the desktop thread every frame). */
void user_wm_tick(void);
void user_wm_input_key(uint8_t key, int pressed);
void user_wm_input_pointer(int x, int y, int pressed);

#endif /* USER_WM_H */
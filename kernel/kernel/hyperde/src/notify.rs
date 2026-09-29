/* ───────────────────────── HyperDE Notifications ─────────────────────────
 * Toast-style notification display ported from upstream HyperDE.
 *
 * Notifications appear as glass-bordered toasts below the top bar,
 * auto-dismiss after a timeout, and stack vertically.
 */

use core::ffi::{c_char, c_int};
use core::sync::atomic::{AtomicBool, Ordering};

use crate::{glass_px, round_rect, draw_text, fill_circle};
use crate::pal;

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
    fn fb_getwidth() -> u32;
    fn fb_getheight() -> u32;
    fn fb_get_pitch() -> c_int;
    fn fb_get_active_buffer() -> *mut u32;
    fn timer_get_milliseconds() -> u64;
}

/* ───────────────────────── Notification storage ───────────────────────── */

const MAX_NOTIFS: usize = 8;
const MAX_MSG: usize = 120;
const TOAST_TIMEOUT_MS: u64 = 4000;
const TOAST_H: i64 = 40;
const TOAST_GAP: i64 = 6;
const TOAST_W: i64 = 360;

#[repr(C)]
#[derive(Copy, Clone)]
struct Notification {
    msg: [u8; MAX_MSG],
    len: u32,
    timestamp: u64,
    active: bool,
    /* color accent for the left edge stripe */
    color: u32,
}

static mut NOTIFS: [Notification; MAX_NOTIFS] = [Notification {
    msg: [0; MAX_MSG],
    len: 0,
    timestamp: 0,
    active: false,
    color: 0x0040D9F0, /* default accent, overridden by config */
}; MAX_NOTIFS];
static NOTIF_DIRTY: AtomicBool = AtomicBool::new(false);

/* ───────────────────────── Public API ───────────────────────── */

/// Push a new notification. msg must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn hyperde_notify(msg: *const c_char, color: u32) {
    if msg.is_null() {
        return;
    }

    /* find a free slot or evict the oldest */
    let mut slot = None;
    let mut oldest_ts = u64::MAX;
    let mut oldest_idx = 0;
    for i in 0..MAX_NOTIFS {
        if !NOTIFS[i].active {
            slot = Some(i);
            break;
        }
        if NOTIFS[i].timestamp < oldest_ts {
            oldest_ts = NOTIFS[i].timestamp;
            oldest_idx = i;
        }
    }
    let idx = slot.unwrap_or(oldest_idx);

    /* copy message */
    let mut len = 0u32;
    let n = &mut NOTIFS[idx];
    while len < MAX_MSG as u32 - 1 && *msg.add(len as usize) != 0 {
        n.msg[len as usize] = *msg.add(len as usize) as u8;
        len += 1;
    }
    n.msg[len as usize] = 0;
    n.len = len;
    n.timestamp = timer_get_milliseconds();
    n.active = true;
    n.color = if color != 0 { color } else { pal().accent };
    NOTIF_DIRTY.store(true, Ordering::Relaxed);

    kprintf(
        b"HYPERDE NOTIFY: %s\0".as_ptr() as *const c_char,
        msg,
    );
}

/// Convenience: green notification (success)
#[no_mangle]
pub unsafe extern "C" fn hyperde_notify_success(msg: *const c_char) {
    hyperde_notify(msg, 0x0030D158); /* green */
}

/// Convenience: red notification (error)
#[no_mangle]
pub unsafe extern "C" fn hyperde_notify_error(msg: *const c_char) {
    hyperde_notify(msg, 0x00FF5F57); /* red */
}

/// Dismiss all active notifications
#[no_mangle]
pub unsafe extern "C" fn hyperde_notify_clear() {
    for i in 0..MAX_NOTIFS {
        NOTIFS[i].active = false;
    }
    NOTIF_DIRTY.store(true, Ordering::Relaxed);
}

/* ───────────────────────── Render ───────────────────────── */

/// Render active notifications as stacked toasts below the bar.
/// Called from the pump when there are active notifications.
pub unsafe fn render_notifications(buf: *mut u32, stride: u32, w: u32, h: u32) {
    let now = timer_get_milliseconds();
    let mut any_active = false;
    let mut y_offset = 36i64; /* below the bar */

    for i in 0..MAX_NOTIFS {
        let n = &NOTIFS[i];
        if !n.active {
            continue;
        }
        let age = now.saturating_sub(n.timestamp);
        if age > TOAST_TIMEOUT_MS {
            NOTIFS[i].active = false;
            continue;
        }
        any_active = true;

        /* fade out in the last 500ms */
        let alpha = if age > TOAST_TIMEOUT_MS - 500 {
            let fade = TOAST_TIMEOUT_MS - age;
            (fade * 0xE0 / 500) as u32
        } else {
            0xE0
        };

        let tx = w as i64 - TOAST_W - 16;
        let ty = y_offset;
        let tw = TOAST_W;
        let th = TOAST_H;

        /* glass background with subtle border */
        round_rect(buf, stride, w, h, tx, ty, tx + tw, ty + th, 10, pal().glass_top, alpha);
        /* outer border (subtle glass edge) */
        for x in tx + 10..tx + tw - 10 {
            glass_px(buf, stride, w, h, x, ty, 0x00FFFFFF, alpha / 6);
            glass_px(buf, stride, w, h, x, ty + th - 1, 0x00000000, alpha / 4);
        }
        /* left accent stripe (color-coded: green=success, red=error, accent=info) */
        for y in ty + 4..ty + th - 4 {
            for x in tx + 4..tx + 7 {
                glass_px(buf, stride, w, h, x, y, n.color, alpha);
            }
        }
        /* top highlight */
        for x in tx + 10..tx + tw - 4 {
            glass_px(buf, stride, w, h, x, ty + 1, 0x00FFFFFF, alpha / 5);
        }
        /* message text */
        draw_text(buf, stride, w, h, tx + 14, ty + (th - 8) / 2, &n.msg[..n.len as usize], pal().text, 1, alpha);
        /* small dismiss dot (top-right) */
        fill_circle(buf, stride, w, h, tx + tw - 12, ty + 10, 2, pal().sub, alpha / 3);

        y_offset += th + TOAST_GAP;
    }

    if any_active {
        NOTIF_DIRTY.store(true, Ordering::Relaxed);
    }
}

/// Check if there are active notifications (for dirty tracking)
pub fn notifications_active() -> bool {
    for i in 0..MAX_NOTIFS {
        if unsafe { NOTIFS[i].active } {
            return true;
        }
    }
    false
}

/// Drain the dirty flag
pub fn drain_dirty() -> bool {
    NOTIF_DIRTY.swap(false, Ordering::Relaxed)
}

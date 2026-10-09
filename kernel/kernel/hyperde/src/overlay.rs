/* ───────────────────────── HyperDE Overlay Module ─────────────────────────
 * Wraps x11_overlay crate for notification popups and launcher overlays.
 * ───────────────────────────────────────────────────────────────────── */

use core::ffi::c_char;

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
}

/* Re-exports from x11_overlay crate */
pub use x11_overlay::{OverlayKind, OverlayManager, Notification};

/* ── Global overlay manager ── */

static mut OVERLAY_MGR: OverlayManager = OverlayManager {
    overlays: [None; 16],
    count: 0,
    next_id: 1,
};

pub fn overlay_manager() -> &'static mut OverlayManager {
    unsafe { &mut OVERLAY_MGR }
}

/// Show a notification overlay (glass morphism style)
pub fn show_notification(title: &str, body: &str) {
    let mgr = overlay_manager();
    if let Some(_notif) = Notification::new(mgr, title, body, 0x0040D9F0) {
        /* Notification created and visible */
        unsafe {
            kprintf(
                b"HYPERDE: notification\0".as_ptr() as *const c_char, 0, 0, 0, 0
            );
        }
    }
}

/// Show the launcher overlay with search
pub fn show_launcher() {
    let mgr = overlay_manager();
    let _ov = mgr.create(
        OverlayKind::Launcher,
        640, 360,  /* centered */
        640, 480,
        200,
    );
}

/// Hide all overlays
pub fn hide_all_overlays() {
    let mgr = overlay_manager();
    mgr.clear_all();
}

/// Tick the overlay system (animate fades)
pub fn overlay_tick(dt_ms: u32) {
    let mgr = overlay_manager();
    mgr.tick_all(dt_ms);
}

/// Get the number of visible overlays
pub fn visible_overlay_count() -> usize {
    let mgr = overlay_manager();
    mgr.visible_count()
}

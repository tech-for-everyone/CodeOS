//! Windowland's compositor half.
//!
//! `codeos.rs` owns layout decisions; this module owns the frame boundary.
//! One `compose_frame()` call composites every X11 surface and then lets the
//! HyperDE shell paint chrome over the result.  The Qt desktop uses that as
//! its only per-frame presentation call, so Windowland owns the event →
//! layout → composite → chrome order.

use core::ffi::c_void;

extern "C" {
    fn prs_compose_frame(wm: *mut c_void);
}

/// Commit one complete desktop frame after layout has been applied.
///
/// The bridge handles surface damage, z-order and the framebuffer flush, then
/// paints the shell overlay. Rust owns *when* that happens so the desktop loop
/// has a single compositor entry point instead of poking `xs_present()` or the
/// shell painter directly.
pub fn compose_frame(wm: *mut c_void) {
    unsafe {
        prs_compose_frame(wm);
    }
}

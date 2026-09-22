#![no_std]
#![no_main]

use core::ffi::{c_char, c_int, c_void};
use core::sync::atomic::{AtomicI32, AtomicU32, AtomicU64, AtomicPtr, Ordering};

/* ── HyperDE crate dependencies ── */
extern crate breadx;
extern crate gdk4_x11;
extern crate x11_overlay;
extern crate mlua;

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
}

mod wayland;
mod config;
mod keybind;
mod launcher;
mod notify;
mod overlay;
mod animation;
mod theme;

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    unsafe {
        kprintf(b"HYPERDE: PANIC in hyperde_shell\0".as_ptr() as *const c_char, 0, 0, 0, 0);
        if let Some(loc) = info.location() {
            kprintf(
                b"  at %s:%u\0".as_ptr() as *const c_char,
                loc.file().as_ptr() as *const c_char,
                loc.line(),
                0,
                0,
            );
        }
    }
    loop {
        unsafe { core::arch::asm!("cli; hlt"); }
    }
}

/* ───────────────────────── font8x8 ASCII glyphs ───────────────────────── */// font8x8 BASIC latin 8x8 glyphs (bit0 = leftmost column). Public domain.
pub const FONT8X8: [[u8; 8]; 128] = [
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x00 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x01 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x02 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x03 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x04 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x05 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x06 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x07 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x08 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x09 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x0a ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x0b ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x0c ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x0d ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x0e ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x0f ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x10 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x11 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x12 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x13 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x14 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x15 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x16 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x17 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x18 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x19 ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x1a ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x1b ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x1c ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x1d ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x1e ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x1f ''
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x20 ' '
    [0x18, 0x3c, 0x3c, 0x18, 0x18, 0x00, 0x18, 0x00], // 0x21 '!'
    [0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x22 '"'
    [0x36, 0x36, 0x7f, 0x36, 0x7f, 0x36, 0x36, 0x00], // 0x23 '#'
    [0x0c, 0x3e, 0x03, 0x1e, 0x30, 0x1f, 0x0c, 0x00], // 0x24 '$'
    [0x00, 0x63, 0x33, 0x18, 0x0c, 0x66, 0x63, 0x00], // 0x25 '%'
    [0x1c, 0x36, 0x1c, 0x6e, 0x3b, 0x33, 0x6e, 0x00], // 0x26 '&'
    [0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x27 '''
    [0x18, 0x0c, 0x06, 0x06, 0x06, 0x0c, 0x18, 0x00], // 0x28 '('
    [0x06, 0x0c, 0x18, 0x18, 0x18, 0x0c, 0x06, 0x00], // 0x29 ')'
    [0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00, 0x00], // 0x2a '*'
    [0x00, 0x0c, 0x0c, 0x3f, 0x0c, 0x0c, 0x00, 0x00], // 0x2b '+'
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x06], // 0x2c ','
    [0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00], // 0x2d '-'
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x00], // 0x2e '.'
    [0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x01, 0x00], // 0x2f '/'
    [0x3e, 0x63, 0x73, 0x7b, 0x6f, 0x67, 0x3e, 0x00], // 0x30 '0'
    [0x0c, 0x0e, 0x0c, 0x0c, 0x0c, 0x0c, 0x3f, 0x00], // 0x31 '1'
    [0x1e, 0x33, 0x30, 0x1c, 0x06, 0x33, 0x3f, 0x00], // 0x32 '2'
    [0x1e, 0x33, 0x30, 0x1c, 0x30, 0x33, 0x1e, 0x00], // 0x33 '3'
    [0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x78, 0x00], // 0x34 '4'
    [0x3f, 0x03, 0x1f, 0x30, 0x30, 0x33, 0x1e, 0x00], // 0x35 '5'
    [0x1c, 0x06, 0x03, 0x1f, 0x33, 0x33, 0x1e, 0x00], // 0x36 '6'
    [0x3f, 0x33, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x00], // 0x37 '7'
    [0x1e, 0x33, 0x33, 0x1e, 0x33, 0x33, 0x1e, 0x00], // 0x38 '8'
    [0x1e, 0x33, 0x33, 0x3e, 0x30, 0x18, 0x0e, 0x00], // 0x39 '9'
    [0x00, 0x0c, 0x0c, 0x00, 0x00, 0x0c, 0x0c, 0x00], // 0x3a ':'
    [0x00, 0x0c, 0x0c, 0x00, 0x00, 0x0c, 0x0c, 0x06], // 0x3b ';'
    [0x18, 0x0c, 0x06, 0x03, 0x06, 0x0c, 0x18, 0x00], // 0x3c '<'
    [0x00, 0x00, 0x3f, 0x00, 0x00, 0x3f, 0x00, 0x00], // 0x3d '='
    [0x06, 0x0c, 0x18, 0x30, 0x18, 0x0c, 0x06, 0x00], // 0x3e '>'
    [0x1e, 0x33, 0x30, 0x18, 0x0c, 0x00, 0x0c, 0x00], // 0x3f '?'
    [0x3e, 0x63, 0x7b, 0x7b, 0x7b, 0x03, 0x1e, 0x00], // 0x40 '@'
    [0x0c, 0x1e, 0x33, 0x33, 0x3f, 0x33, 0x33, 0x00], // 0x41 'A'
    [0x3f, 0x66, 0x66, 0x3e, 0x66, 0x66, 0x3f, 0x00], // 0x42 'B'
    [0x3c, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3c, 0x00], // 0x43 'C'
    [0x1f, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1f, 0x00], // 0x44 'D'
    [0x7f, 0x46, 0x16, 0x1e, 0x16, 0x46, 0x7f, 0x00], // 0x45 'E'
    [0x7f, 0x46, 0x16, 0x1e, 0x16, 0x06, 0x0f, 0x00], // 0x46 'F'
    [0x3c, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7c, 0x00], // 0x47 'G'
    [0x33, 0x33, 0x33, 0x3f, 0x33, 0x33, 0x33, 0x00], // 0x48 'H'
    [0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0x00], // 0x49 'I'
    [0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1e, 0x00], // 0x4a 'J'
    [0x67, 0x66, 0x36, 0x1e, 0x36, 0x66, 0x67, 0x00], // 0x4b 'K'
    [0x0f, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7f, 0x00], // 0x4c 'L'
    [0x63, 0x77, 0x7f, 0x7f, 0x6b, 0x63, 0x63, 0x00], // 0x4d 'M'
    [0x63, 0x67, 0x6f, 0x7b, 0x73, 0x63, 0x63, 0x00], // 0x4e 'N'
    [0x1c, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1c, 0x00], // 0x4f 'O'
    [0x3f, 0x66, 0x66, 0x3e, 0x06, 0x06, 0x0f, 0x00], // 0x50 'P'
    [0x1e, 0x33, 0x33, 0x33, 0x3b, 0x1e, 0x38, 0x00], // 0x51 'Q'
    [0x3f, 0x66, 0x66, 0x3e, 0x36, 0x66, 0x67, 0x00], // 0x52 'R'
    [0x1e, 0x33, 0x07, 0x0e, 0x38, 0x33, 0x1e, 0x00], // 0x53 'S'
    [0x3f, 0x2d, 0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0x00], // 0x54 'T'
    [0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3f, 0x00], // 0x55 'U'
    [0x33, 0x33, 0x33, 0x33, 0x33, 0x1e, 0x0c, 0x00], // 0x56 'V'
    [0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0x00], // 0x57 'W'
    [0x63, 0x63, 0x36, 0x1c, 0x1c, 0x36, 0x63, 0x00], // 0x58 'X'
    [0x33, 0x33, 0x33, 0x1e, 0x0c, 0x0c, 0x1e, 0x00], // 0x59 'Y'
    [0x7f, 0x63, 0x31, 0x18, 0x4c, 0x66, 0x7f, 0x00], // 0x5a 'Z'
    [0x1e, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1e, 0x00], // 0x5b '['
    [0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x40, 0x00], // 0x5c '\'
    [0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1e, 0x00], // 0x5d ']'
    [0x08, 0x1c, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00], // 0x5e '^'
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff], // 0x5f '_'
    [0x0c, 0x0c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x60 '`'
    [0x00, 0x00, 0x1e, 0x30, 0x3e, 0x33, 0x6e, 0x00], // 0x61 'a'
    [0x07, 0x06, 0x06, 0x3e, 0x66, 0x66, 0x3b, 0x00], // 0x62 'b'
    [0x00, 0x00, 0x1e, 0x33, 0x03, 0x33, 0x1e, 0x00], // 0x63 'c'
    [0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6e, 0x00], // 0x64 'd'
    [0x00, 0x00, 0x1e, 0x33, 0x3f, 0x03, 0x1e, 0x00], // 0x65 'e'
    [0x1c, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0f, 0x00], // 0x66 'f'
    [0x00, 0x00, 0x6e, 0x33, 0x33, 0x3e, 0x30, 0x1f], // 0x67 'g'
    [0x07, 0x06, 0x36, 0x6e, 0x66, 0x66, 0x67, 0x00], // 0x68 'h'
    [0x0c, 0x00, 0x0e, 0x0c, 0x0c, 0x0c, 0x1e, 0x00], // 0x69 'i'
    [0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1e], // 0x6a 'j'
    [0x07, 0x06, 0x66, 0x36, 0x1e, 0x36, 0x67, 0x00], // 0x6b 'k'
    [0x0e, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0x00], // 0x6c 'l'
    [0x00, 0x00, 0x33, 0x7f, 0x7f, 0x6b, 0x63, 0x00], // 0x6d 'm'
    [0x00, 0x00, 0x1f, 0x33, 0x33, 0x33, 0x33, 0x00], // 0x6e 'n'
    [0x00, 0x00, 0x1e, 0x33, 0x33, 0x33, 0x1e, 0x00], // 0x6f 'o'
    [0x00, 0x00, 0x3b, 0x66, 0x66, 0x3e, 0x06, 0x0f], // 0x70 'p'
    [0x00, 0x00, 0x6e, 0x33, 0x33, 0x3e, 0x30, 0x78], // 0x71 'q'
    [0x00, 0x00, 0x3b, 0x6e, 0x66, 0x06, 0x0f, 0x00], // 0x72 'r'
    [0x00, 0x00, 0x3e, 0x03, 0x1e, 0x30, 0x1f, 0x00], // 0x73 's'
    [0x08, 0x0c, 0x3e, 0x0c, 0x0c, 0x2c, 0x18, 0x00], // 0x74 't'
    [0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6e, 0x00], // 0x75 'u'
    [0x00, 0x00, 0x33, 0x33, 0x33, 0x1e, 0x0c, 0x00], // 0x76 'v'
    [0x00, 0x00, 0x63, 0x6b, 0x7f, 0x7f, 0x36, 0x00], // 0x77 'w'
    [0x00, 0x00, 0x63, 0x36, 0x1c, 0x36, 0x63, 0x00], // 0x78 'x'
    [0x00, 0x00, 0x33, 0x33, 0x33, 0x3e, 0x30, 0x1f], // 0x79 'y'
    [0x00, 0x00, 0x3f, 0x19, 0x0c, 0x26, 0x3f, 0x00], // 0x7a 'z'
    [0x38, 0x0c, 0x0c, 0x07, 0x0c, 0x0c, 0x38, 0x00], // 0x7b '{'
    [0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00], // 0x7c '|'
    [0x07, 0x0c, 0x0c, 0x38, 0x0c, 0x0c, 0x07, 0x00], // 0x7d '}'
    [0x6e, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x7e '~'
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], // 0x7f ''
];
/* ───────────────────────── kernel C ABI ───────────────────────── */

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RtcTime {
    pub second: c_int,
    pub minute: c_int,
    pub hour: c_int,
    pub day: c_int,
    pub month: c_int,
    pub year: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LvglRect {
    pub x: c_int,
    pub y: c_int,
    pub w: c_int,
    pub h: c_int,
}

#[repr(C)]
#[allow(dead_code)]
pub struct LvglWindow {
    pub id: c_int,
    pub title: [u8; 128],
    pub rect: LvglRect,
    pub obj: *mut c_void,
    pub visible: c_int,
    pub focused: c_int,
}

extern "C" {
    fn fb_getwidth() -> u32;
    fn fb_getheight() -> u32;
    fn fb_get_pitch() -> c_int;
    fn fb_get_bpp() -> u8;
    fn fb_get_active_buffer() -> *mut u32;
    fn rtc_read(t: *mut RtcTime);
    fn timer_get_milliseconds() -> u64;
    fn sched_busy_ticks() -> u64;
    fn pmm_count_used() -> u64;
    fn pmm_total_pages() -> u64;
    fn lvgl_wm_window_count(wm: *const c_void) -> c_int;
    fn lvgl_wm_window_at(wm: *const c_void, idx: c_int) -> *const LvglWindow;
}

/* ── X11/GNUstep windows as first-class desktop citizens ────────────
 * Layout mirrors prs_desktop_win_t in kernel/kernel/penrose_bridge.h. */

const PRS_WIN_MAX: usize = 32;

#[repr(C)]
#[derive(Copy, Clone)]
struct PrsDesktopWin {
    xid: u32,
    x: i16,
    y: i16,
    w: i16,
    h: i16,
    mapped: u8,
    focused: u8,
    title: [u8; 40],
}

const PRS_WIN_ZERO: PrsDesktopWin = PrsDesktopWin {
    xid: 0,
    x: 0,
    y: 0,
    w: 0,
    h: 0,
    mapped: 0,
    focused: 0,
    title: [0; 40],
};

extern "C" {
    fn prs_desktop_windows(buf: *mut PrsDesktopWin, max: c_int) -> c_int;
}

/* ───────────────────────── palette (XRGB8888) ─────────────────────────
 * COSMIC-style: default cyan accent, glass chrome rendered high-key so
 * the bar reads as a single translucent slab (rounded, floating).
 * Defaults are overridden by conf.lua at boot via palette_load_config(). */

struct Palette {
    accent: u32, green: u32, text: u32, sub: u32,
    glass_top: u32, glass_bot: u32, glass_alpha: u32,
    focused_border: u32, normal_border: u32, border_width: u32, bar_h: u32,
}
static mut PAL: Palette = Palette {
    accent: 0x0040D9F0, green: 0x0030D158, text: 0x00F5F5F7, sub: 0x0095A0AA,
    glass_top: 0x00242428, glass_bot: 0x0017171B, glass_alpha: 0xC8,
    focused_border: 0x0040D9F0, normal_border: 0x003c3836, border_width: 2, bar_h: 28,
};
const CLOSE_DOT: u32 = 0x00FF5F57;
const MIN_DOT: u32 = 0x00FFBD2E;
const MAX_DOT: u32 = 0x0028C840;

/// Load palette from conf.lua config into the mutable palette
unsafe fn palette_load_config() {
    let cfg = config::config();
    PAL.accent = cfg.accent;
    PAL.focused_border = cfg.focused_border;
    PAL.normal_border = cfg.normal_border;
    PAL.glass_top = cfg.glass_top;
    PAL.glass_bot = cfg.glass_bottom;
    PAL.glass_alpha = cfg.glass_alpha;
    PAL.border_width = cfg.border_width;
    PAL.bar_h = cfg.panel_height;
}

/// Read-only palette access
#[inline(always)]
unsafe fn pal() -> &'static Palette { &*(&raw const PAL) }

/* ───────────────────────── state ─────────────────────────
 * Everything shared between the Qt panels thread (pump / set_active) and the
 * QPA input path (hyperde_shell_active) is atomic so the crate is safe to
 * build with SMP enabled. */

static FB_W: AtomicU32 = AtomicU32::new(0);
static FB_H: AtomicU32 = AtomicU32::new(0);
static ACTIVE: AtomicI32 = AtomicI32::new(1);
static DRAWS: AtomicU64 = AtomicU64::new(0);
static LAST_MS: AtomicU64 = AtomicU64::new(0);
static LAST_MIN: AtomicI32 = AtomicI32::new(-1);
static LAST_CPU: AtomicI32 = AtomicI32::new(-1);
static LAST_MEM: AtomicU64 = AtomicU64::new(0);
static INIT_DONE: AtomicI32 = AtomicI32::new(0);
static CM_WM: AtomicPtr<c_void> = AtomicPtr::new(core::ptr::null_mut());
static LAST_WMSIG: AtomicU64 = AtomicU64::new(u64::MAX);
static LAST_XSIG: AtomicU64 = AtomicU64::new(u64::MAX);
/* workspace indicator state (kept in sync by the Qt WM via hyperde_shell_set_workspace) */
static WS_CUR: AtomicI32 = AtomicI32::new(0);
static WS_NUM: AtomicI32 = AtomicI32::new(6);

const RELAX: Ordering = Ordering::Relaxed;
const ACQREL: Ordering = Ordering::AcqRel;

/* ───────────────────────── primitives ───────────────────────── */

unsafe fn blend(base: u32, g: u32, alpha: u32) -> u32 {
    if alpha >= 0xFF {
        return g;
    }
    if alpha == 0 {
        return base;
    }
    let a = alpha & 0xFF;
    let inv = 256 - a;
    let r = (((g >> 16) & 0xFF) * a + ((base >> 16) & 0xFF) * inv) / 256;
    let gr = (((g >> 8) & 0xFF) * a + ((base >> 8) & 0xFF) * inv) / 256;
    let bl = ((g & 0xFF) * a + (base & 0xFF) * inv) / 256;
    (r << 16) | (gr << 8) | bl
}

unsafe fn px(buf: *mut u32, stride: u32, w: u32, h: u32, x: i64, y: i64, color: u32) {
    if x < 0 || y < 0 || x >= w as i64 || y >= h as i64 {
        return;
    }
    *buf.add((y as u32 * stride + x as u32) as usize) = color;
}

unsafe fn glass_px(buf: *mut u32, stride: u32, w: u32, h: u32, x: i64, y: i64, g: u32, alpha: u32) {
    if x < 0 || y < 0 || x >= w as i64 || y >= h as i64 {
        return;
    }
    let i = (y as u32 * stride + x as u32) as usize;
    *buf.add(i) = blend(*buf.add(i), g, alpha);
}

unsafe fn fill_rect(buf: *mut u32, stride: u32, w: u32, h: u32, x0: i64, y0: i64, x1: i64, y1: i64, c: u32, alpha: u32) {
    let (x0, x1) = (x0.max(0), x1.min(w as i64 - 1));
    let (y0, y1) = (y0.max(0), y1.min(h as i64 - 1));
    for y in y0..=y1 {
        for x in x0..=x1 {
            glass_px(buf, stride, w, h, x, y, c, alpha);
        }
    }
}

unsafe fn fill_circle(buf: *mut u32, stride: u32, w: u32, h: u32, cx: i64, cy: i64, r: i64, c: u32, _alpha: u32) {
    let rr = r * r;
    for y in cy - r..=cy + r {
        for x in cx - r..=cx + r {
            let dx = x - cx;
            let dy = y - cy;
            if dx * dx + dy * dy <= rr {
                px(buf, stride, w, h, x, y, c);
            }
        }
    }
}

fn glyph_bit(glyph: &[u8; 8], row: usize, col: usize) -> bool {
    (glyph[row] >> col) & 1 != 0
}

unsafe fn draw_text(
    buf: *mut u32,
    stride: u32,
    w: u32,
    h: u32,
    mut x: i64,
    y0: i64,
    s: &[u8],
    color: u32,
    scale: i64,
    alpha: u32,
) -> i64 {
    let _ = alpha;
    for &ch in s {
        if ch >= 0x80 {
            x += 8 * scale;
            continue;
        }
        if ch != b' ' {
            let glyph = FONT8X8[ch as usize];
            for row in 0..8usize {
                for col in 0..8usize {
                    if glyph_bit(&glyph, row, col) {
                        if scale == 1 {
                            px(buf, stride, w, h, x + col as i64, y0 + row as i64, color);
                        } else {
                            for dy in 0..scale {
                                for dx in 0..scale {
                                    px(buf, stride, w, h, x + col as i64 * scale + dx, y0 + row as i64 * scale + dy, color);
                                }
                            }
                        }
                    }
                }
            }
        }
        x += 8 * scale + 2 * scale;
    }
    x
}

unsafe fn round_rect(buf: *mut u32, stride: u32, w: u32, h: u32, x0: i64, y0: i64, x1: i64, y1: i64, r: i64, c: u32, alpha: u32) {
    fill_rect(buf, stride, w, h, x0 + r, y0, x1 - r, y1, c, alpha);
    fill_rect(buf, stride, w, h, x0, y0 + r, x1, y1 - r, c, alpha);
    fill_circle(buf, stride, w, h, x0 + r, y0 + r, r, c, alpha);
    fill_circle(buf, stride, w, h, x1 - r, y0 + r, r, c, alpha);
    fill_circle(buf, stride, w, h, x0 + r, y1 - r, r, c, alpha);
    fill_circle(buf, stride, w, h, x1 - r, y1 - r, r, c, alpha);
}

/* ───────────────────────── helpers ───────────────────────── */

fn fmt_int(mut v: c_int, pad: usize, out: &mut [u8]) -> usize {
    if v < 0 {
        v = 0;
    }
    let mut tmp = [0u8; 10];
    let mut i = 0usize;
    if v == 0 {
        tmp[i] = b'0';
        i += 1;
    } else {
        while v > 0 && i < tmp.len() {
            tmp[i] = b'0' + (v % 10) as u8;
            i += 1;
            v /= 10;
        }
    }
    let cap = out.len();
    let mut n = 0usize;
    while n < pad.saturating_sub(i) && n < cap {
        out[n] = b'0';
        n += 1;
    }
    let mut j = i;
    while j > 0 && n < cap {
        j -= 1;
        out[n] = tmp[j];
        n += 1;
    }
    n
}

unsafe fn rtc_now() -> RtcTime {
    let mut t = RtcTime { second: 0, minute: 0, hour: 0, day: 0, month: 0, year: 0 };
    rtc_read(&mut t);
    t
}

unsafe fn cpu_percent() -> c_int {
    /* same formula as SysMon: total = ms/10 tick units, busy = sched_busy_ticks */
    static LAST_TOTAL: AtomicU64 = AtomicU64::new(0);
    static LAST_BUSY: AtomicU64 = AtomicU64::new(0);
    let total = timer_get_milliseconds() / 10;
    let busy = sched_busy_ticks();
    let mut pct: c_int = 0;
    let pt = LAST_TOTAL.load(RELAX);
    let pb = LAST_BUSY.load(RELAX);
    let dbusy = busy.saturating_sub(pb);
    let dtotal = total.saturating_sub(pt);
    if pt != 0 && dtotal > 0 {
        pct = (dbusy.saturating_mul(100) / dtotal) as c_int;
        if pct > 100 {
            pct = 100;
        }
    }
    LAST_TOTAL.store(total, RELAX);
    LAST_BUSY.store(busy, RELAX);
    pct
}

unsafe fn mem_mb() -> u64 {
    pmm_count_used() * 4096 / 1024 / 1024
}

unsafe fn window_overview(wm: *const c_void) -> ([usize; 32], usize) {
    let mut list = [0usize; 32];
    let c = if wm.is_null() { 0 } else { lvgl_wm_window_count(wm) };
    let n = if c < 0 { 0 } else { c as usize }.min(32);
    let mut idx = 0usize;
    for i in 0..n {
        let ww = lvgl_wm_window_at(wm, i as c_int);
        if !ww.is_null() && (*ww).focused != 0 {
            list[idx] = i;
            idx += 1;
        }
    }
    for i in 0..n {
        let ww = lvgl_wm_window_at(wm, i as c_int);
        if !ww.is_null() && (*ww).focused == 0 {
            list[idx] = i;
            idx += 1;
        }
    }
    (list, idx)
}

/* ───────────────────────── bar render ─────────────────────────
 * COSMIC-style panel: app-grid launcher button left, window task
 * pills, centered clock, compact applet tiles (net/cpu/mem) and
 * battery/power glyphs on the right. */

const MONTHS: [[u8; 3]; 12] = [
    *b"JAN", *b"FEB", *b"MAR", *b"APR", *b"MAY", *b"JUN",
    *b"JUL", *b"AUG", *b"SEP", *b"OCT", *b"NOV", *b"DEC",
];

unsafe fn render_bar(buf: *mut u32, stride: u32, w: u32, h: u32) {
    let mid = (pal().bar_h / 2) as i64;

    /* liquid glass base (vertical gradient, blended over underlying) */
    for y in 0..pal().bar_h as i64 {
        let t = (y as u32 * 255) / pal().bar_h;
        let top_r = (pal().glass_top >> 16) & 0xFF;
        let top_g = (pal().glass_top >> 8) & 0xFF;
        let top_b = pal().glass_top & 0xFF;
        let bot_r = (pal().glass_bot >> 16) & 0xFF;
        let bot_g = (pal().glass_bot >> 8) & 0xFF;
        let bot_b = pal().glass_bot & 0xFF;
        let r = (top_r * (255 - t) + bot_r * t) / 255;
        let g2 = (top_g * (255 - t) + bot_g * t) / 255;
        let b = (top_b * (255 - t) + bot_b * t) / 255;
        let gcol = (r << 16) | (g2 << 8) | b;
        for x in 0..w as i64 {
            glass_px(buf, stride, w, h, x, y, gcol, pal().glass_alpha);
        }
    }

    /* top hairline highlight, bottom accent + shadow */
    for x in 0..w as i64 {
        glass_px(buf, stride, w, h, x, 0, 0x00FFFFFF, 0x22);
        glass_px(buf, stride, w, h, x, 1, 0x00FFFFFF, 0x12);
        glass_px(buf, stride, w, h, x, pal().bar_h as i64 - 2, pal().accent, 0x46);
        glass_px(buf, stride, w, h, x, pal().bar_h as i64 - 1, 0x00000000, 0x34);
    }

    /* app-grid launcher button (COSMIC "Applications" activity) */
    let lx = 10i64;
    round_rect(buf, stride, w, h, lx, mid - 11, lx + 36, mid + 11, 10, 0x00FFFFFF, 0x1A);
    let lc = lx + 18;
    for gy in -1i64..=1 {
        for gx in -1i64..=1 {
            fill_circle(buf, stride, w, h, lc + gx * 6, mid + gy * 6, 2, pal().accent, 0xFF);
        }
    }
    /* divider */
    for y in mid - 10..mid + 10 {
        glass_px(buf, stride, w, h, 54, y, 0x00FFFFFF, 0x18);
    }

    /* ── workspace indicator (COSMIC: stacked bars right of the divider) ── */
    let ws_num = WS_NUM.load(RELAX).clamp(1, 9) as i64;
    let ws_cur = (WS_CUR.load(RELAX) as i64).clamp(0, ws_num - 1);
    let wbar_w = 14i64;
    let wbar_h = 4i64;
    let ws_start = 62i64;
    for i in 0..ws_num {
        let wx = ws_start + i * 22;
        let wy = mid - wbar_h / 2;
        if i == ws_cur {
            round_rect(buf, stride, w, h, wx, wy - 1, wx + wbar_w, wy + wbar_h + 1, 3, pal().accent, 0xFF);
        } else {
            round_rect(buf, stride, w, h, wx, wy, wx + wbar_w, wy + wbar_h, 2, 0x00FFFFFF, 0x26);
        }
    }

    /* task pills from the window manager (cap before the center clock) */
    let wm = CM_WM.load(RELAX);
    let (list, lcount) = window_overview(wm);
    let mut x = 62 + ws_num * 22;
    let mut k = 0usize;
    while k < lcount {
        if x > (w as i64) / 2 - 150 {
            break;
        }
        let ww = lvgl_wm_window_at(wm, list[k] as c_int);
        k += 1;
        if ww.is_null() {
            continue;
        }
        let focused = (*ww).focused != 0;
        let mut s = [0u8; 9];
        let mut n = 0usize;
        for cc in (*ww).title {
            if n >= 9 {
                break;
            }
            if cc == 0 {
                break;
            }
            s[n] = cc as u8;
            n += 1;
        }
        let tw = n as i64 * 10 + 12;
        round_rect(buf, stride, w, h, x, mid - 9, x + tw, mid + 9, 9, 0x00FFFFFF, 0x16);
        fill_circle(buf, stride, w, h, x + 7, mid, 3, if focused { pal().green } else { pal().sub }, 0xA0);
        draw_text(buf, stride, w, h, x + 14, mid - 4, &s[..n], if focused { pal().text } else { pal().sub }, 1, 0xFF);
        x += tw + 8;
    }

    /* X11/GNUstep task pills follow the lvgl pills (same layout so the
     * hit-test in hyperde_shell_bar_hit matches 1:1) */
    {
        let mut xwins = [PRS_WIN_ZERO; PRS_WIN_MAX];
        let nc = prs_desktop_windows(xwins.as_mut_ptr(), PRS_WIN_MAX as c_int) as usize;
        let mut i = 0usize;
        while i < nc {
            if x > (w as i64) / 2 - 150 {
                break;
            }
            let xwin = xwins[i];
            i += 1;
            if xwin.mapped == 0 {
                continue;
            }
            let mut n = 0usize;
            while n < 9 && xwin.title[n] != 0 {
                n += 1;
            }
            let tw = n as i64 * 10 + 12;
            round_rect(buf, stride, w, h, x, mid - 9, x + tw, mid + 9, 9, 0x00FFFFFF, 0x16);
            fill_circle(buf, stride, w, h, x + 7, mid, 3, if xwin.focused != 0 { pal().green } else { pal().sub }, 0xA0);
            draw_text(buf, stride, w, h, x + 14, mid - 4, &xwin.title[..n], if xwin.focused != 0 { pal().text } else { pal().sub }, 1, 0xFF);
            x += tw + 8;
        }
    }

    let rh = 18i64;
    let ry = mid - 9;

    /* ── battery + power glyph tiles (far right) ── */
    let pow_x = w as i64 - 34;
    let bat_x = w as i64 - 68;
    round_rect(buf, stride, w, h, pow_x, ry, pow_x + 28, ry + rh, 9, 0x00FFFFFF, 0x12);
    round_rect(buf, stride, w, h, bat_x, ry, bat_x + 28, ry + rh, 9, 0x00FFFFFF, 0x12);
    /* power: concentric ring + stem */
    fill_circle(buf, stride, w, h, pow_x + 14, mid, 4, pal().sub, 0xC0);
    fill_circle(buf, stride, w, h, pow_x + 14, mid, 2, pal().glass_bot, 0xF0);
    for dy in -6i64..-2 {
        px(buf, stride, w, h, pow_x + 14, mid + dy, pal().sub);
    }
    /* battery: outline + 70% fill + nub */
    for dy in -3i64..=3 {
        px(buf, stride, w, h, bat_x + 16, mid + dy, pal().sub);
        px(buf, stride, w, h, bat_x + 22, mid + dy, pal().sub);
    }
    for dx in 0i64..8 {
        for dy in -4i64..=4 {
            let c = if dx < 6 { pal().accent } else { pal().sub };
            px(buf, stride, w, h, bat_x + 16 + dx, mid + dy, c);
        }
    }
    for dy in -1i64..=1 {
        px(buf, stride, w, h, bat_x + 24, mid + dy, pal().sub);
    }

    /* ── applet tiles: NET, CPU, MEM ── */
    let net_x = w as i64 - 118;
    let cpu_x = w as i64 - 178;
    let mem_x = w as i64 - 238;
    round_rect(buf, stride, w, h, net_x, ry, net_x + 40, ry + rh, 9, 0x00FFFFFF, 0x12);
    fill_circle(buf, stride, w, h, net_x + 10, mid, 3, pal().green, 0xA0);
    draw_text(buf, stride, w, h, net_x + 19, mid - 4, b"net", pal().sub, 1, 0xFF);

    let cpu = cpu_percent();
    round_rect(buf, stride, w, h, cpu_x, ry, cpu_x + 52, ry + rh, 9, 0x00FFFFFF, 0x12);
    draw_text(buf, stride, w, h, cpu_x + 8, mid - 4, b"cpu", pal().sub, 1, 0xFF);
    let mut cbuf = [0u8; 16];
    let cl = fmt_int(cpu, 0, &mut cbuf);
    draw_text(buf, stride, w, h, cpu_x + 40 - cl as i64 * 10, mid - 4, &cbuf[..cl], pal().text, 1, 0xFF);

    let mem = mem_mb() as c_int;
    round_rect(buf, stride, w, h, mem_x, ry, mem_x + 52, ry + rh, 9, 0x00FFFFFF, 0x12);
    draw_text(buf, stride, w, h, mem_x + 8, mid - 4, b"mem", pal().sub, 1, 0xFF);
    let mut mbuf = [0u8; 16];
    let ml = fmt_int(mem, 0, &mut mbuf);
    draw_text(buf, stride, w, h, mem_x + 40 - ml as i64 * 10, mid - 4, &mbuf[..ml], pal().text, 1, 0xFF);

    /* ── centered clock: small date + scale-2 time ── */
    let now = rtc_now();
    let mut hbuf = [0u8; 8];
    let hl = fmt_int(now.hour, 0, &mut hbuf);
    let mut m2 = [0u8; 8];
    let ml2 = fmt_int(now.minute, 2, &mut m2);
    let mut dbuf = [0u8; 8];
    let mut dn = 0usize;
    if now.month >= 1 && now.month <= 12 {
        for cc in MONTHS[now.month as usize - 1] {
            dbuf[dn] = cc;
            dn += 1;
        }
    }
    dbuf[dn] = b' ';
    dn += 1;
    let mut dd = [0u8; 4];
    let ddl = fmt_int(now.day, 0, &mut dd);
    for i in 0..ddl {
        dbuf[dn + i] = dd[i];
    }
    dn += ddl;
    let dw = dn as i64 * 10;
    let tww = ((hl + 1 + ml2) as i64) * 20;
    let start = (w as i64) / 2 - (dw + 8 + tww) / 2;
    draw_text(buf, stride, w, h, start, mid - 4, &dbuf[..dn], pal().sub, 1, 0xFF);
    let tx = start + dw + 8;
    draw_text(buf, stride, w, h, tx, mid - 8, &hbuf[..hl], pal().text, 2, 0xFF);
    draw_text(buf, stride, w, h, tx + hl as i64 * 20, mid - 8, b":", pal().text, 2, 0xFF);
    draw_text(buf, stride, w, h, tx + (hl as i64 + 1) * 20, mid - 8, &m2[..ml2], pal().text, 2, 0xFF);
}

/* ───────────────────── window compositor ─────────────────────
 * HyperDE owns the window chrome: for every visible WM window it
 * composites a COSMIC-style title band (rounded, glass), centered
 * title, window controls on the RIGHT of the band and a focus
 * accent in the accent color, over whatever Qt painted. Qt stays
 * in charge of the window content area and input; this layer simply
 * re-skins the decorations so the compositor and the panel read as
 * one COSMIC slab.
 */

const WIN_TB: i64 = 30; /* title band height (matches QtAppWindow tb) */
const WIN_SH: i64 = 6; /* shadow inset (matches QtAppWindow sh) */
const WIN_CR: i64 = 12; /* rounded band corners (matches QtAppWindow cr) */

unsafe fn render_windows(buf: *mut u32, stride: u32, w: u32, h: u32) {
    let wm = CM_WM.load(RELAX);
    let (list, lcount) = window_overview(wm);
    /* window_overview lists focused windows first; draw in reverse so the
     * focused window ends up on top of the stack. */
    let mut k = lcount;
    while k > 0 {
        k -= 1;
        let ww = lvgl_wm_window_at(wm, list[k] as c_int);
        if ww.is_null() || (*ww).visible == 0 {
            continue;
        }
        let rx = (*ww).rect.x as i64;
        let ry = (*ww).rect.y as i64;
        let rw = (*ww).rect.w as i64;
        let rh = (*ww).rect.h as i64;
        if rw <= 0 || rh <= 0 {
            continue;
        }
        /* entirely above the bar — nothing to compose */
        if ry + rh <= pal().bar_h as i64 {
            continue;
        }
        let mut s = [0u8; 40];
        let mut n = 0usize;
        for cc in (*ww).title {
            if n >= 40 {
                break;
            }
            if cc == 0 {
                break;
            }
            s[n] = cc as u8;
            n += 1;
        }
        draw_window_chrome(buf, stride, w, h, rx, ry, rw, rh, (*ww).focused != 0, &s[..n]);
    }

    /* X11/GNUstep windows get the same macOS chrome. penrose windows
     * composite above the Qt stack, so they are drawn after (on top of)
     * lvgl windows, in reverse so raised windows stay on top. */
    let mut xwins = [PRS_WIN_ZERO; PRS_WIN_MAX];
    let nc = prs_desktop_windows(xwins.as_mut_ptr(), PRS_WIN_MAX as c_int) as usize;
    let mut i = nc;
    while i > 0 {
        i -= 1;
        let win = xwins[i];
        if win.mapped == 0 {
            continue;
        }
        let rx = win.x as i64;
        let ry = win.y as i64;
        let rw = win.w as i64;
        let rh = win.h as i64;
        if rw <= 0 || rh <= 0 {
            continue;
        }
        /* entirely above the bar — nothing to compose */
        if ry + rh <= pal().bar_h as i64 {
            continue;
        }
        let mut n = 0usize;
        while n < win.title.len() && win.title[n] != 0 {
            n += 1;
        }
        draw_window_chrome(buf, stride, w, h, rx, ry, rw, rh, win.focused != 0, &win.title[..n]);
    }
}

/* macOS-style chrome for one window body, shared by the lvgl/Qt pass and
 * the X11/GNUstep pass so both window families look identical: layered
 * drop shadow, glass title band with rounded top corners + specular edge,
 * COSMIC traffic-light controls, centered title, accent focus ring. */
unsafe fn draw_window_chrome(
    buf: *mut u32, stride: u32, w: u32, h: u32,
    rx: i64, ry: i64, rw: i64, rh: i64,
    focused: bool, title: &[u8],
) {
    /* layered drop shadow at bottom edge */
    fill_rect(buf, stride, w, h, rx + WIN_SH + 3, ry + rh + 7, rx + rw - WIN_SH - 3, ry + rh + 9, 0x00000000, 0x08);
    fill_rect(buf, stride, w, h, rx + WIN_SH + 1, ry + rh + 4, rx + rw - WIN_SH - 1, ry + rh + 6, 0x00000000, 0x10);
    fill_rect(buf, stride, w, h, rx + WIN_SH, ry + rh + 1, rx + rw - WIN_SH, ry + rh + 3, 0x00000000, 0x1E);

    /* title band: glass gradient, rounded top corners */
    let by0 = ry + WIN_SH;
    let by1 = by0 + WIN_TB;
    let x0 = rx + WIN_SH;
    let x1 = rx + rw - WIN_SH - 1;
    for y in by0..by1 {
        if y < pal().bar_h as i64 {
            continue;
        }
        let t = ((y - by0) * 255 / WIN_TB) as u32;
        let top_r = (pal().glass_top >> 16) & 0xFF;
        let top_g = (pal().glass_top >> 8) & 0xFF;
        let top_b = pal().glass_top & 0xFF;
        let bot_r = (pal().glass_bot >> 16) & 0xFF;
        let bot_g = (pal().glass_bot >> 8) & 0xFF;
        let bot_b = pal().glass_bot & 0xFF;
        let r = (top_r * (255 - t) + bot_r * t) / 255;
        let g = (top_g * (255 - t) + bot_g * t) / 255;
        let b = (top_b * (255 - t) + bot_b * t) / 255;
        let gcol = (r << 16) | (g << 8) | b;
        /* rounded corner clipping at the top */
        let dy = y - by0;
        let mut cl = 0i64;
        let mut cx = 0i64;
        if dy < WIN_CR {
            let side = isqrt64(WIN_CR * WIN_CR - (WIN_CR - dy) * (WIN_CR - dy));
            cl = WIN_CR - side;
            cx = WIN_CR - side;
        }
        for x in x0 + cl..=x1 - cx {
            glass_px(buf, stride, w, h, x, y, gcol, if focused { 0xE8 } else { 0xB0 });
        }
    }
    /* top specular + bottom separator (accent when focused) */
    for x in x0..=x1 {
        glass_px(buf, stride, w, h, x, by0, 0x00FFFFFF, 0x22);
        glass_px(buf, stride, w, h, x, by0 + 1, 0x00FFFFFF, 0x12);
        if focused {
            glass_px(buf, stride, w, h, x, by1 - 1, pal().accent, 0x88);
        } else {
            glass_px(buf, stride, w, h, x, by1 - 1, 0x00000000, 0x33);
        }
    }

    /* window controls on the RIGHT of the band (COSMIC style) */
    let dot_y = by0 + (WIN_TB - 12) / 2;
    let gap = 20i64;
    let close_x = rx + rw - WIN_SH - 18; /* rightmost = close */
    let (dc, dm, dx) = if focused {
        (CLOSE_DOT, MIN_DOT, MAX_DOT)
    } else {
        (0x005A5A5E, 0x005A5A5E, 0x005A5A5E)
    };
    fill_circle(buf, stride, w, h, close_x, dot_y + 6, 6, dc, 0xFF);
    fill_circle(buf, stride, w, h, close_x - gap, dot_y + 6, 6, dm, 0xFF);
    fill_circle(buf, stride, w, h, close_x - gap * 2, dot_y + 6, 6, dx, 0xFF);

    /* centered title between the left inset and the controls */
    let tw = title.len() as i64 * 10;
    let tcx = (x0 + close_x - gap * 2) / 2;
    draw_text(
        buf, stride, w, h,
        tcx - tw / 2,
        by0 + (WIN_TB - 8) / 2 - 1,
        title,
        if focused { pal().text } else { pal().sub },
        1, 0xFF,
    );

    /* focus ring: 1px accent rect around the body */
    if focused {
        for x in x0..=x1 {
            glass_px(buf, stride, w, h, x, by1 - 1, pal().accent, 0x88);
            glass_px(buf, stride, w, h, x, ry + rh - WIN_SH, pal().accent, 0x38);
        }
        for y in by0..ry + rh - WIN_SH {
            glass_px(buf, stride, w, h, x0, y, pal().accent, 0x30);
            glass_px(buf, stride, w, h, x1, y, pal().accent, 0x30);
        }
    }
}

/* very small integer sqrt for the corner clipping above */
fn isqrt64(v: i64) -> i64 {
    if v <= 0 {
        return 0;
    }
    let mut x = v;
    let mut y = (v + 1) / 2;
    while y < x {
        x = y;
        y = (x + v / x) / 2;
    }
    x
}

/* ───────────────────────── exports ───────────────────────── */

#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_init() {
    if INIT_DONE.compare_exchange(0, 1, ACQREL, RELAX).is_err() {
        return;
    }
    FB_W.store(fb_getwidth(), RELAX);
    FB_H.store(fb_getheight(), RELAX);
    kprintf(b"HYPERDE: rust shell ready fb=%dx%d bpp=%d\0".as_ptr() as *const c_char,
        FB_W.load(RELAX) as c_int, FB_H.load(RELAX) as c_int, fb_get_bpp() as c_int);
    kprintf(b"HYPERDE: compositor active=%d\0".as_ptr() as *const c_char, ACTIVE.load(RELAX));

    wayland::hyperde_wl_selftest();

    /* load conf.lua config into mutable palette */
    config::config_parse_lua(include_str!("../../../../userspace/hyperde/conf.lua").as_bytes());
    palette_load_config();
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_active() -> c_int {
    ACTIVE.load(RELAX)
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_set_active(v: c_int) -> c_int {
    let old = ACTIVE.swap(if v != 0 { 1 } else { 0 }, ACQREL);
    if old != (if v != 0 { 1 } else { 0 }) {
        kprintf(b"HYPERDE: compositor switched %d->%d\0".as_ptr() as *const c_char, old, if v != 0 { 1 } else { 0 });
    }
    ACTIVE.load(RELAX)
}

/* keep the bar's workspace indicator in sync with the Qt tiling manager */
#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_set_workspace(cur: c_int, num: c_int) {
    WS_CUR.store(cur, RELAX);
    WS_NUM.store(num, RELAX);
}

/* live system readings for the Qt quick-settings toast */
#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_cpu() -> c_int {
    cpu_percent()
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_mem_mb() -> c_int {
    mem_mb() as c_int
}

/* Total physical RAM the PMM manages, in MiB.  The Qt quick-settings meter
 * uses this so it scales to the machine instead of assuming a fixed size. */
#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_mem_total_mb() -> c_int {
    (pmm_total_pages() * 4096 / 1024 / 1024) as c_int
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_pump(wm: *mut c_void) {
    if INIT_DONE.load(RELAX) == 0 {
        hyperde_shell_init();
    }
    if ACTIVE.load(RELAX) == 0 {
        return;
    }
    CM_WM.store(wm, RELAX);

    if FB_W.load(RELAX) == 0 {
        FB_W.store(fb_getwidth(), RELAX);
        FB_H.store(fb_getheight(), RELAX);
        if FB_W.load(RELAX) == 0 {
            return;
        }
    }

    let now = timer_get_milliseconds();
    let t = rtc_now();
    let cpu = cpu_percent();
    let mem = mem_mb();

    /* Repaint only when something user-visible changed. An unconditional
     * tick-based repaint re-blends the translucent glass over whatever Qt
     * is writing underneath, which makes the top bar's pixels wobble every
     * ~200ms (perceived as flashing near the panel). */
    let mut dirty = t.minute != LAST_MIN.load(RELAX)
        || cpu.abs_diff(LAST_CPU.load(RELAX) as c_int) >= 5
        || mem != LAST_MEM.load(RELAX);
    if INIT_DONE.load(RELAX) == 1 {
        /* first frame must always paint, even when idle */
        if DRAWS.load(RELAX) == 0 {
            dirty = true;
        }
    }
    /* repaint immediately when the window set changes (open/close/focus) */
    {
        let wm = CM_WM.load(RELAX);
        let (list, lcount) = window_overview(wm);
        let mut sig: u64 = lcount as u64;
        if lcount > 0 {
            let ww = lvgl_wm_window_at(wm, list[0] as c_int);
            if !ww.is_null() {
                sig = sig.wrapping_mul(31).wrapping_add((*ww).focused as u64 * 7 + (*ww).visible as u64 * 3 + 1);
            }
        }
        if sig != LAST_WMSIG.load(RELAX) {
            LAST_WMSIG.store(sig, RELAX);
            dirty = true;
        }
    }
    /* X11/GNUstep windows share the compositor with the bar: repaint when
     * the X11 window set changes (open/close/focus/geometry) or the chrome
     * drawn by render_windows goes stale under later body composites. */
    {
        let mut xwins = [PRS_WIN_ZERO; PRS_WIN_MAX];
        let nx = prs_desktop_windows(xwins.as_mut_ptr(), PRS_WIN_MAX as c_int) as usize;
        let mut xsig: u64 = (nx as u64).wrapping_add(1);
        for wi in 0..nx {
            let w2 = &xwins[wi];
            let mut hsh: u64 = w2.xid as u64;
            hsh = hsh.rotate_left(5) ^ ((w2.x as u64) & 0xFFFF);
            hsh = hsh.rotate_left(5) ^ ((w2.y as u64) & 0xFFFF);
            hsh = hsh.rotate_left(5) ^ ((w2.w as u64) & 0xFFFF);
            hsh = hsh.rotate_left(5) ^ ((w2.h as u64) & 0xFFFF);
            hsh = hsh.rotate_left(5)
                ^ ((w2.mapped as u64).wrapping_mul(3) + (w2.focused as u64).wrapping_mul(7));
            xsig = xsig.rotate_left(7) ^ hsh;
        }
        if xsig != LAST_XSIG.load(RELAX) {
            LAST_XSIG.store(xsig, RELAX);
            dirty = true;
        }
    }

    let w = FB_W.load(RELAX);
        let h = FB_H.load(RELAX);
        let pitch = fb_get_pitch();
        let row = if pitch > 0 { (pitch as u32) / 4 } else { w };
        let buf = fb_get_active_buffer();
        /* Window chrome repaints on every pass: the X11 flush composites
         * opaque window bodies after the previous chrome draw, so an
         * always-on chrome pass keeps bands/traffic lights on top. Bodies
         * are static, so re-blending the glass over them is deterministic
         * (no bar-style flicker); the bar stays dirty-gated. */
        render_windows(buf, row, w, h);
        if dirty {
            render_bar(buf, row, w, h);
            DRAWS.fetch_add(1, RELAX);
            kprintf(b"HYPERDE: render #%u\0".as_ptr() as *const c_char, DRAWS.load(RELAX) as u32);
            LAST_MS.store(now, RELAX);
            LAST_MIN.store(t.minute, RELAX);
            LAST_CPU.store(cpu, RELAX);
            LAST_MEM.store(mem, RELAX);
            if DRAWS.load(RELAX) % 20 == 0 {
                kprintf(b"HYPERDE: bar #%u hh=%d mm=%d cpu=%d mem=%llu\0".as_ptr() as *const c_char,
                    DRAWS.load(RELAX) as u32, t.hour, t.minute, cpu, mem);
            }
        }
}

/* ───────────────────────── bar hit-test (HyperDE input) ─────────────────────────
 * Returns the bar element at (mx,my) using the same layout math as render_bar so
 * the click strip and the painted chrome never drift apart:
 *   1       → logo/app name (open launcher)
 *   900     → NET pill    901 → CPU pill   902 → MEM pill   903 → clock
 *   1000+i  → task pill for wm window index i (focus it)
 *   0       → nothing
 */

#[no_mangle]
pub unsafe extern "C" fn hyperde_shell_bar_hit(mx: c_int, my: c_int) -> c_int {
    if my < 0 || my >= pal().bar_h as c_int {
        return 0;
    }
    let w = FB_W.load(RELAX) as i64;
    if w <= 0 || mx < 0 {
        return 0;
    }
    let x = mx as i64;

    /* launcher button (also the whole leading region) */
    if x < 54 {
        return 1;
    }

    /* ── workspace indicator bars → 920+i; task pills shift right ── */
    let ws_num = WS_NUM.load(RELAX).clamp(1, 9) as i64;
    let ws_start = 62i64;
    if x >= ws_start && x < ws_start + ws_num * 22 {
        let idx = ((x - ws_start) / 22) as c_int;
        if idx >= 0 && idx < ws_num as c_int {
            return 920 + idx;
        }
    }

    let wm = CM_WM.load(RELAX);
    let (list, lcount) = window_overview(wm);
    let mut px = ws_start + ws_num * 22;
    let mut k = 0usize;
    while k < lcount {
        if px > (w as i64) / 2 - 150 {
            break;
        }
        let idx = list[k];
        k += 1;
        let ww = lvgl_wm_window_at(wm, idx as c_int);
        if ww.is_null() {
            continue;
        }
        let mut n = 0usize;
        for cc in (*ww).title {
            if n >= 9 {
                break;
            }
            if cc == 0 {
                break;
            }
            n += 1;
        }
        let tw = n as i64 * 10 + 12;
        if x >= px && x < px + tw {
            return (1000 + idx) as c_int;
        }
        px += tw + 8;
    }

    /* X11/GNUstep pills → 2000 + index (mirrors the render_bar layout) */
    {
        let mut xwins = [PRS_WIN_ZERO; PRS_WIN_MAX];
        let nc = prs_desktop_windows(xwins.as_mut_ptr(), PRS_WIN_MAX as c_int) as usize;
        let mut i = 0usize;
        while i < nc {
            if px > (w as i64) / 2 - 150 {
                break;
            }
            let xwin = xwins[i];
            i += 1;
            if xwin.mapped == 0 {
                continue;
            }
            let mut n = 0usize;
            while n < 9 && xwin.title[n] != 0 {
                n += 1;
            }
            let tw = n as i64 * 10 + 12;
            if x >= px && x < px + tw {
                return 2000 + (i - 1) as c_int;
            }
            px += tw + 8;
        }
    }

    /* battery + power glyph tiles (far right) */
    if x >= w - 68 && x < w - 34 {
        return 904; /* battery */
    }
    if x >= w - 34 {
        return 905; /* power */
    }

    /* applet tiles: NET, CPU, MEM */
    let net_x = w - 118;
    let cpu_x = w - 178;
    let mem_x = w - 238;
    if x >= mem_x && x < mem_x + 52 {
        return 902;
    }
    if x >= cpu_x && x < cpu_x + 52 {
        return 901;
    }
    if x >= net_x && x < net_x + 40 {
        return 900;
    }

    /* centered clock region → 903 */
    let midx = w / 2;
    if (x - midx).abs() < 110 {
        return 903;
    }
    0
}
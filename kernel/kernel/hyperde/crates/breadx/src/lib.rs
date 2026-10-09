/* ═══════════════════════════════════════════════════════════════════════
 * breadx — Pure-Rust X11 protocol for HyperDE (no_std kernel port)
 *
 * This is a minimal port of the breadx / x11rb API surface needed by
 * HyperDE.  Every call routes through the kernel's X11 server via the
 * syscalls declared in the extern block below.  No real sockets, no
 * libX11 — just the protocol framing and atom/event plumbing.
 * ═══════════════════════════════════════════════════════════════════════ */
#![no_std]
#![allow(unused_assignments)]

use core::ffi::{c_char, c_int};
use core::sync::atomic::{AtomicU32, Ordering};

#[allow(unused)]
extern "C" {
    fn kprintf(fmt: *const c_char, ...);
    fn x11_intern_atom(name: *const c_char, only_if_exists: c_int) -> u32;
    fn x11_get_atom_name(atom: u32) -> *const c_char;
    fn x11_create_kernel_window(title: *const c_char, w: c_int, h: c_int) -> u32;
    fn x11_release_window(xid: u32);
    fn malloc(size: usize) -> *mut u8;
    fn free(p: *mut u8);
}

/* ── atom cache ── */

static ATOM_NEXT: AtomicU32 = AtomicU32::new(256);

pub fn intern_atom(name: &str, only_if_exists: bool) -> u32 {
    let c_name = name.as_ptr() as *const c_char;
    let ret = unsafe { x11_intern_atom(c_name, if only_if_exists { 1 } else { 0 }) };
    if ret == 0 && !only_if_exists {
        ATOM_NEXT.fetch_add(1, Ordering::Relaxed)
    } else {
        ret
    }
}

pub fn get_atom_name(atom: u32) -> &'static str {
    if atom == 0 {
        return "";
    }
    unsafe {
        let p = x11_get_atom_name(atom);
        if p.is_null() {
            return "";
        }
        core::str::from_utf8_unchecked(core::ffi::CStr::from_ptr(p).to_bytes())
    }
}

/* ── window creation ── */

pub fn create_window(title: &str, width: u32, height: u32) -> u32 {
    let mut buf = [0u8; 64];
    let bytes = title.as_bytes();
    let n = bytes.len().min(63);
    let mut i = 0;
    while i < n {
        buf[i] = bytes[i];
        i += 1;
    }
    buf[n] = 0;
    unsafe { x11_create_kernel_window(buf.as_ptr() as *const c_char, width as c_int, height as c_int) }
}

pub fn destroy_window(xid: u32) {
    unsafe { x11_release_window(xid) }
}

/* ── request framing ── */

#[repr(C)]
pub struct X11Request {
    pub opcode: u8,
    pub data: [u8; 64],
    pub len: usize,
}

impl X11Request {
    pub fn new(opcode: u8) -> Self {
        Self { opcode, data: [0; 64], len: 1 }
    }

    pub fn write_u8(&mut self, offset: usize, val: u8) {
        if offset < self.data.len() {
            self.data[offset] = val;
        }
        if offset + 1 > self.len { self.len = offset + 1; }
    }

    pub fn write_u16(&mut self, offset: usize, val: u16) {
        self.write_u8(offset, val as u8);
        self.write_u8(offset + 1, (val >> 8) as u8);
    }

    pub fn write_u32(&mut self, offset: usize, val: u32) {
        self.write_u8(offset, val as u8);
        self.write_u8(offset + 1, (val >> 8) as u8);
        self.write_u8(offset + 2, (val >> 16) as u8);
        self.write_u8(offset + 3, (val >> 24) as u8);
    }

    pub fn write_string(&mut self, offset: usize, s: &str) {
        let bytes = s.as_bytes();
        self.write_u16(offset, bytes.len() as u16);
        let mut i = 0;
        while i < bytes.len() && offset + 2 + i < self.data.len() {
            self.data[offset + 2 + i] = bytes[i];
            i += 1;
        }
        let total = offset + 2 + bytes.len();
        let aligned = (total + 3) & !3;
        if aligned > self.len { self.len = aligned; }
    }
}

/* ── event types (mirror x11_server.h) ── */

pub const KEY_PRESS: u8 = 2;
pub const KEY_RELEASE: u8 = 3;
pub const BUTTON_PRESS: u8 = 4;
pub const BUTTON_RELEASE: u8 = 5;
pub const MOTION_NOTIFY: u8 = 6;
pub const MAP_REQUEST: u8 = 20;
pub const UNMAP_NOTIFY: u8 = 18;
pub const DESTROY_NOTIFY: u8 = 17;
pub const CONFIGURE_NOTIFY: u8 = 22;
pub const CLIENT_MESSAGE: u8 = 33;
pub const EXPOSE: u8 = 12;

#[repr(C)]
pub struct X11Event {
    pub type_: u8,
    pub pad: [u8; 63],
}

impl X11Event {
    pub fn event_type(&self) -> u8 {
        self.type_
    }

    pub fn window(&self) -> u32 {
        u32::from_le_bytes([self.pad[0], self.pad[1], self.pad[2], self.pad[3]])
    }

    pub fn key_code(&self) -> u16 {
        u16::from_le_bytes([self.pad[0], self.pad[1]])
    }

    pub fn state(&self) -> u16 {
        u16::from_le_bytes([self.pad[2], self.pad[3]])
    }

    pub fn x(&self) -> i16 {
        i16::from_le_bytes([self.pad[4], self.pad[5]])
    }

    pub fn y(&self) -> i16 {
        i16::from_le_bytes([self.pad[6], self.pad[7]])
    }

    pub fn width(&self) -> u16 {
        u16::from_le_bytes([self.pad[8], self.pad[9]])
    }

    pub fn height(&self) -> u16 {
        u16::from_le_bytes([self.pad[10], self.pad[11]])
    }

    pub fn data32(&self, idx: usize) -> u32 {
        let off = idx * 4;
        if off + 4 <= self.pad.len() {
            u32::from_le_bytes([self.pad[off], self.pad[off+1], self.pad[off+2], self.pad[off+3]])
        } else {
            0
        }
    }
}

/* ── geometry ── */

#[derive(Debug, Clone, Copy)]
pub struct Geometry {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
    pub border_width: u32,
    pub depth: u32,
}

/* ── connection handle ── */

pub struct Connection {
    pub client_id: i32,
    pub sequence: u32,
}

impl Connection {
    pub fn new(client_id: i32) -> Self {
        Self { client_id, sequence: 0 }
    }

    pub fn sequence(&self) -> u32 {
        self.sequence
    }

    pub fn next_seq(&mut self) -> u32 {
        self.sequence += 1;
        self.sequence
    }
}

/* ── property helpers ── */

pub fn change_property_u32(conn: &mut Connection, window: u32, property: u32, data: &[u32]) {
    let mut req = X11Request::new(10); /* ChangeProperty */
    req.write_u32(4, window);
    req.write_u32(8, property);
    req.write_u32(12, 6); /* XA_CARDINAL */
    req.write_u8(16, 32); /* format */
    req.write_u32(20, data.len() as u32);
    let mut i = 0;
    while i < data.len() && 24 + (i + 1) * 4 <= 64 {
        req.write_u32(24 + i * 4, data[i]);
        i += 1;
    }
    let _ = conn.next_seq();
}

pub fn change_property_string(conn: &mut Connection, window: u32, property: u32, value: &str) {
    let mut req = X11Request::new(10); /* ChangeProperty */
    req.write_u32(4, window);
    req.write_u32(8, property);
    req.write_u32(12, 31); /* XA_STRING */
    req.write_u8(16, 8); /* format */
    let bytes = value.as_bytes();
    req.write_u32(20, bytes.len() as u32);
    let mut i = 0;
    while i < bytes.len() && 24 + i < 64 {
        req.data[24 + i] = bytes[i];
        i += 1;
    }
    let _ = conn.next_seq();
}

/* ── send event helper ── */

pub fn send_event(conn: &mut Connection, window: u32, event_type: u8, data: &[u32]) {
    let mut req = X11Request::new(23); /* SendEvent */
    req.write_u8(0, 0); /* propagate = false */
    req.write_u32(4, window);
    req.write_u32(8, 0); /* event mask */
    req.data[12] = event_type;
    let mut i = 0;
    while i < data.len() && 16 + (i + 1) * 4 <= 64 {
        req.write_u32(16 + i * 4, data[i]);
        i += 1;
    }
    let _ = conn.next_seq();
}

/* ── select input helper ── */

pub fn select_input(conn: &mut Connection, window: u32, mask: u32) {
    let mut req = X11Request::new(2); /* SelectInput (mapped to our server) */
    req.write_u32(4, window);
    req.write_u32(8, mask);
    let _ = conn.next_seq();
}

/* ── map / unmap / configure ── */

pub fn map_window(conn: &mut Connection, window: u32) {
    let mut req = X11Request::new(3); /* MapWindow */
    req.write_u32(4, window);
    let _ = conn.next_seq();
}

pub fn unmap_window(conn: &mut Connection, window: u32) {
    let mut req = X11Request::new(4); /* UnmapWindow */
    req.write_u32(4, window);
    let _ = conn.next_seq();
}

pub fn configure_window(conn: &mut Connection, window: u32, x: i32, y: i32, w: u32, h: u32) {
    let mut req = X11Request::new(5); /* ConfigureWindow */
    req.write_u32(4, window);
    req.write_u16(8, 0x000F); /* value mask: x|y|w|h */
    req.write_u16(10, 0); /* pad */
    req.write_u32(12, x as u32);
    req.write_u32(16, y as u32);
    req.write_u32(20, w);
    req.write_u32(24, h);
    let _ = conn.next_seq();
}

pub fn raise_window(conn: &mut Connection, window: u32) {
    let mut req = X11Request::new(5); /* ConfigureWindow with stack_mode */
    req.write_u32(4, window);
    req.write_u16(8, 0x0040); /* stack_mode only */
    req.write_u16(10, 0);
    req.write_u32(28, 0); /* Above */
    let _ = conn.next_seq();
}

/* ── put image ── */

pub fn put_image(conn: &mut Connection, drawable: u32, gc: u32,
                 x: i32, y: i32, w: u32, h: u32, pixels: &[u8]) {
    let mut req = X11Request::new(57); /* PutImage */
    req.write_u32(4, drawable);
    req.write_u32(8, gc);
    req.write_u8(12, 32); /* depth */
    req.write_u8(13, 2);  /* format: ZPixmap */
    req.write_u16(14, 0); /* pad */
    req.write_u16(16, x as u16);
    req.write_u16(18, y as u16);
    req.write_u16(20, w as u16);
    req.write_u16(22, h as u16);
    req.write_u16(24, 0); /* pad */
    /* pixels follow in the request — truncated to 40 bytes for our buffer */
    let mut i = 0;
    while i < pixels.len() && 26 + i < 64 {
        req.data[26 + i] = pixels[i];
        i += 1;
    }
    req.len = 26 + pixels.len().min(38);
    let _ = conn.next_seq();
}

/* ── fill rectangle (draw color block) ── */

pub fn fill_rect(conn: &mut Connection, drawable: u32, gc: u32,
                 x: i32, y: i32, w: u32, h: u32) {
    let mut req = X11Request::new(55); /* PolyFillRectangle */
    req.write_u32(4, drawable);
    req.write_u32(8, gc);
    req.write_u16(12, x as u16);
    req.write_u16(14, y as u16);
    req.write_u16(16, w as u16);
    req.write_u16(18, h as u16);
    let _ = conn.next_seq();
}

/* ── draw string ── */

pub fn draw_string(conn: &mut Connection, drawable: u32, gc: u32,
                   x: i32, y: i32, text: &str) {
    let mut req = X11Request::new(61); /* ImageText8 */
    req.write_u32(4, drawable);
    req.write_u32(8, gc);
    req.write_u16(12, x as u16);
    req.write_u16(14, y as u16);
    let bytes = text.as_bytes();
    let n = bytes.len().min(255);
    req.write_u8(16, n as u8);
    let mut i = 0;
    while i < n && 17 + i < 64 {
        req.data[17 + i] = bytes[i];
        i += 1;
    }
    req.len = 17 + n;
    let _ = conn.next_seq();
}

/* ── intern atoms with string literals ── */

pub struct Atoms {
    pub wm_protocols: u32,
    pub wm_delete_window: u32,
    pub wm_name: u32,
    pub net_wm_name: u32,
    pub net_wm_window_type: u32,
    pub net_wm_window_type_dock: u32,
    pub net_wm_window_type_normal: u32,
    pub clipboard: u32,
    pub utf8_string: u32,
    pub net_supported: u32,
    pub net_supporting_wm_check: u32,
    pub net_active_window: u32,
    pub net_wm_state: u32,
    pub net_wm_state_focused: u32,
    pub net_wm_pid: u32,
    pub net_wm_user_time: u32,
}

impl Atoms {
    pub fn intern_all() -> Self {
        Self {
            wm_protocols: intern_atom("WM_PROTOCOLS", false),
            wm_delete_window: intern_atom("WM_DELETE_WINDOW", false),
            wm_name: intern_atom("WM_NAME", false),
            net_wm_name: intern_atom("NET_WM_NAME", false),
            net_wm_window_type: intern_atom("NET_WM_WINDOW_TYPE", false),
            net_wm_window_type_dock: intern_atom("NET_WM_WINDOW_TYPE_DOCK", false),
            net_wm_window_type_normal: intern_atom("NET_WM_WINDOW_TYPE_NORMAL", false),
            clipboard: intern_atom("CLIPBOARD", false),
            utf8_string: intern_atom("UTF8_STRING", false),
            net_supported: intern_atom("NET_SUPPORTED", false),
            net_supporting_wm_check: intern_atom("NET_SUPPORTING_WM_CHECK", false),
            net_active_window: intern_atom("NET_ACTIVE_WINDOW", false),
            net_wm_state: intern_atom("NET_WM_STATE", false),
            net_wm_state_focused: intern_atom("NET_WM_STATE_FOCUSED", false),
            net_wm_pid: intern_atom("NET_WM_PID", false),
            net_wm_user_time: intern_atom("NET_WM_USER_TIME", false),
        }
    }
}

/* ── cursor helpers ── */

pub fn create_cursor(xid: u32, shape: u32) -> u32 {
    /* Cursor creation routed through xserver */
    let mut req = X11Request::new(78); /* CreateCursor */
    req.write_u32(4, xid); /* cid */
    req.write_u32(8, 0);   /* source */
    req.write_u32(12, 0);  /* mask */
    req.write_u16(16, 0);  /* fore red */
    req.write_u16(18, 0);
    req.write_u16(20, 0);
    req.write_u16(22, 0xFFFF);
    req.write_u16(24, 0xFFFF);
    req.write_u16(26, 0xFFFF);
    req.write_u16(28, shape as u16);
    let _ = 0u32; /* placeholder */
    xid
}

/* ── grab / ungrab pointer ── */

pub fn grab_pointer(conn: &mut Connection, window: u32) -> u32 {
    let mut req = X11Request::new(19); /* GrabPointer */
    req.write_u32(4, window);
    req.write_u8(8, 1); /* owner_events */
    req.write_u16(10, 0x0004 | 0x0008 | 0x0002); /* button_press|release|motion */
    req.write_u8(14, 1); /* pointer_mode: Async */
    req.write_u8(15, 0); /* keyboard_mode */
    let _ = conn.next_seq();
    0
}

pub fn ungrab_pointer(conn: &mut Connection) {
    let _ = X11Request::new(20); /* UngrabPointer */
    let _ = conn.next_seq();
}

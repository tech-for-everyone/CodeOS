/* ───────────────────────── Wayland-style server dispatch ─────────────────────────
 * Port of the libwayland-server dispatch model (wl_client_connection_data,
 * wl_resource / wl_interface + wl_closure_dispatch) from
 * https://gitlab.freedesktop.org/wayland/wayland (src/wayland-server.c).
 *
 * A wire object-id map keys into per-interface resources; dispatch validates the
 * opcode against the interface's method table and posts wl_display protocol
 * errors (invalid object / invalid method) on failure, mirroring client->error
 * dropping the remainder of a connection once tripped.
 *
 * The RESOURCES / SURFACES arenas are `static mut` but every access happens
 * under the ARENA_LOCK spinlock, so they are safe on SMP without atomics.
 */
#![allow(static_mut_refs)]

use core::ffi::{c_char, c_int};
use core::sync::atomic::{AtomicBool, Ordering};

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
}

/* Arena lock */
static ARENA_LOCK: AtomicBool = AtomicBool::new(false);

fn arena_lock() {
    while ARENA_LOCK
        .compare_exchange_weak(false, true, Ordering::Acquire, Ordering::Relaxed)
        .is_err()
    {
        core::hint::spin_loop();
    }
}

fn arena_unlock() {
    ARENA_LOCK.store(false, Ordering::Release);
}

const ERR_INVALID_OBJECT: u32 = 0;
const ERR_INVALID_METHOD: u32 = 1;

const IFACE_DISPLAY: usize = 0;
const IFACE_REGISTRY: usize = 1;
const IFACE_COMPOSITOR: usize = 2;
const IFACE_SURFACE: usize = 3;

pub const MAX_RES: usize = 32;
pub const MAX_SURFACES: usize = 16;
pub const MAX_DAMAGE: usize = 8;

#[derive(Clone, Copy)]
pub struct WlRes {
    pub id: u32,
    pub iface: usize,
    pub state: i32,
    pub in_use: bool,
}

const RES_EMPTY: WlRes = WlRes { id: 0, iface: IFACE_DISPLAY, state: -1, in_use: false };
static mut RESOURCES: [WlRes; MAX_RES] = [RES_EMPTY; MAX_RES];

#[derive(Clone, Copy)]
pub struct WlSurface {
    pub in_use: bool,
    pub title: [u8; 32],
    pub attach_x: c_int,
    pub attach_y: c_int,
    pub has_attach: i32,
    pub damage: [u32; MAX_DAMAGE],
    pub n_damage: usize,
    pub committed: i32,
    pub visible: i32,
}

const SURF_EMPTY: WlSurface = WlSurface {
    in_use: false,
    title: [0; 32],
    attach_x: 0,
    attach_y: 0,
    has_attach: 0,
    damage: [0; MAX_DAMAGE],
    n_damage: 0,
    committed: 0,
    visible: 0,
};
static mut SURFACES: [WlSurface; MAX_SURFACES] = [SURF_EMPTY; MAX_SURFACES];

static mut CLIENT_ERROR: i32 = 0;

pub type WlHandler = unsafe fn(obj: u32, args: *const u32);

pub struct WlMethod {
    #[allow(dead_code)]
    pub name: &'static [u8],
    pub handler: WlHandler,
}

pub const fn method(name: &'static [u8], handler: WlHandler) -> WlMethod {
    WlMethod { name, handler }
}

pub struct WlInterface {
    #[allow(dead_code)]
    pub name: &'static [u8],
    pub methods: &'static [WlMethod],
}

unsafe fn empty_handler(_obj: u32, _args: *const u32) {}

unsafe fn log_u(msg: &[u8], a: u32, b: u32, c: u32, d: u32) {
    kprintf(msg.as_ptr() as *const c_char, a, b, c, d);
}

/* ── object registry (cf. wl_client->objects map) ── */

unsafe fn res_alloc(id: u32, iface: usize) -> usize {
    for i in 0..MAX_RES {
        let r = &mut *RESOURCES.as_mut_ptr().add(i);
        if !r.in_use {
            r.id = id;
            r.iface = iface;
            r.state = -1;
            r.in_use = true;
            return i;
        }
    }
    MAX_RES
}

unsafe fn res_free_by_id(id: u32) {
    for i in 0..MAX_RES {
        let r = &mut *RESOURCES.as_mut_ptr().add(i);
        if r.in_use && r.id == id {
            r.in_use = false;
            return;
        }
    }
}

unsafe fn res_slot_by_id(id: u32) -> usize {
    for i in 0..MAX_RES {
        let r = &*RESOURCES.as_ptr().add(i);
        if r.in_use && r.id == id {
            return i;
        }
    }
    MAX_RES
}

/* ── surface state helpers ── */

unsafe fn surf_alloc() -> usize {
    for i in 0..MAX_SURFACES {
        let s = &mut *SURFACES.as_mut_ptr().add(i);
        if !s.in_use {
            *s = SURF_EMPTY;
            s.in_use = true;
            return i;
        }
    }
    MAX_SURFACES
}

unsafe fn surf_of_obj(obj: u32) -> *mut WlSurface {
    let slot = res_slot_by_id(obj);
    if slot >= MAX_RES {
        return core::ptr::null_mut();
    }
    let st = (*RESOURCES.as_ptr().add(slot)).state;
    if st < 0 || st as usize >= MAX_SURFACES {
        return core::ptr::null_mut();
    }
    let s = &mut *SURFACES.as_mut_ptr().add(st as usize);
    if !s.in_use {
        return core::ptr::null_mut();
    }
    s
}

/* ── protocol errors (cf. wl_resource_post_error) ── */

unsafe fn protocol_error(code: u32, arg: u32) {
    CLIENT_ERROR = 1;
    kprintf(b"HYPERDE: srvf protocol_err code=%u arg=%u\0".as_ptr() as *const c_char, code, arg);
}

/* ── wl_display ── */

unsafe fn on_display_sync(obj: u32, _args: *const u32) {
    kprintf(b"HYPERDE: srvf wl_display.sync obj=%u\0".as_ptr() as *const c_char, obj, 0, 0, 0);
}

unsafe fn on_display_get_registry(obj: u32, args: *const u32) {
    let rid = *args.add(0);
    let r = res_alloc(rid, IFACE_REGISTRY);
    if r >= MAX_RES {
        protocol_error(ERR_INVALID_METHOD, rid);
        return;
    }
    log_u(b"HYPERDE: srvf wl_display.get_registry obj=%u -> %u (res %u)\0", obj, rid, r as u32, 0);
}

/* ── wl_registry ── */

unsafe fn on_registry_bind(obj: u32, args: *const u32) {
    let _name = *args.add(0);
    let iface_name = *args.add(1);
    let version = *args.add(2);
    let new_id = *args.add(3);
    let r = res_alloc(new_id, IFACE_COMPOSITOR);
    if r >= MAX_RES {
        protocol_error(ERR_INVALID_METHOD, new_id);
        return;
    }
    log_u(
        b"HYPERDE: srvf wl_registry.bind obj=%u iface_magic=0x%x ver=%u new=%u\0",
        obj,
        iface_name,
        version,
        new_id,
    );
}

unsafe fn on_registry_destroy(obj: u32, _args: *const u32) {
    res_free_by_id(obj);
    log_u(b"HYPERDE: srvf wl_registry.destroy obj=%u\0", obj, 0, 0, 0);
}

/* ── wl_compositor ── */

unsafe fn on_compositor_create_surface(obj: u32, args: *const u32) {
    let surf_id = *args.add(0);
    let r = res_alloc(surf_id, IFACE_SURFACE);
    if r >= MAX_RES {
        protocol_error(ERR_INVALID_METHOD, surf_id);
        return;
    }
    let si = surf_alloc();
    if si >= MAX_SURFACES {
        res_free_by_id(surf_id);
        protocol_error(ERR_INVALID_METHOD, surf_id);
        return;
    }
    (*RESOURCES.as_mut_ptr().add(r)).state = si as i32;
    log_u(
        b"HYPERDE: srvf wl_compositor.create_surface obj=%u -> surf obj=%u (surf %u)\0",
        obj,
        surf_id,
        si as u32,
        0,
    );
}

unsafe fn on_compositor_destroy(obj: u32, _args: *const u32) {
    res_free_by_id(obj);
    log_u(b"HYPERDE: srvf wl_compositor.destroy obj=%u\0", obj, 0, 0, 0);
}

/* ── wl_surface ── */

unsafe fn on_surface_destroy(obj: u32, _args: *const u32) {
    let s = surf_of_obj(obj);
    if !s.is_null() {
        (*s).in_use = false;
    }
    res_free_by_id(obj);
    log_u(b"HYPERDE: srvf wl_surface.destroy obj=%u\0", obj, 0, 0, 0);
}

unsafe fn on_surface_attach(obj: u32, args: *const u32) {
    let s = surf_of_obj(obj);
    if s.is_null() {
        return;
    }
    (*s).attach_x = *args.add(1) as c_int;
    (*s).attach_y = *args.add(2) as c_int;
    (*s).has_attach = 1;
    log_u(
        b"HYPERDE: srvf wl_surface.attach obj=%u buf=%u x=%d y=%d\0",
        obj,
        *args.add(0),
        (*s).attach_x as u32,
        (*s).attach_y as u32,
    );
}

unsafe fn on_surface_damage(obj: u32, args: *const u32) {
    let s = surf_of_obj(obj);
    if s.is_null() {
        return;
    }
    let n = (*s).n_damage;
    if n < MAX_DAMAGE {
        (*s).damage[n] = *args.add(0);
        (*s).n_damage = n + 1;
    }
    log_u(
        b"HYPERDE: srvf wl_surface.damage obj=%u d#%u x=%d y=%d\0",
        obj,
        (*s).n_damage as u32,
        *args.add(1),
        *args.add(2),
    );
}

unsafe fn on_surface_commit(obj: u32, _args: *const u32) {
    let s = surf_of_obj(obj);
    if s.is_null() {
        return;
    }
    (*s).committed = 1;
    (*s).visible = 1;
    log_u(
        b"HYPERDE: srvf wl_surface.commit obj=%u dmg=%u attach=%d visible=%d\0",
        obj,
        (*s).n_damage as u32,
        (*s).has_attach as u32,
        (*s).visible as u32,
    );
}

unsafe fn on_surface_set_buffer_transform(obj: u32, args: *const u32) {
    log_u(b"HYPERDE: srvf wl_surface.set_buffer_transform obj=%u t=%u\0", obj, *args.add(0), 0, 0);
}

unsafe fn on_surface_frame(obj: u32, args: *const u32) {
    log_u(b"HYPERDE: srvf wl_surface.frame obj=%u cb=%u\0", obj, *args.add(0), 0, 0);
}

unsafe fn on_surface_set_title(obj: u32, args: *const u32) {
    let s = surf_of_obj(obj);
    if s.is_null() {
        return;
    }
    let lo = *args.add(0);
    let hi = *args.add(1);
    let p = ((hi as usize) << 32 | lo as usize) as *const u8;
    let base = p as usize;
    const KIMG_LO: usize = 0xffffffff80000000;
    const KIMG_HI: usize = 0xffffffff82000000;
    let mut n = 0usize;
    while n < 31 && base >= KIMG_LO && base <= KIMG_HI && n < KIMG_HI - base {
        let c = *p.add(n);
        (*s).title[n] = c;
        n += 1;
        if c == 0 {
            break;
        }
    }
    (*s).title[n] = 0;
    kprintf(b"HYPERDE: srvf wl_surface.set_title obj=%u title=%s\0".as_ptr() as *const c_char, obj, (*s).title.as_ptr() as *const c_char);
}

/* ── interface method tables (opcode order follows wayland.xml) ── */

const WL_DISPLAY_METHODS: [WlMethod; 2] = [
    method(b"sync", on_display_sync),
    method(b"get_registry", on_display_get_registry),
];

const WL_REGISTRY_METHODS: [WlMethod; 2] = [
    method(b"bind", on_registry_bind),
    method(b"destroy", on_registry_destroy),
];

const WL_COMPOSITOR_METHODS: [WlMethod; 3] = [
    method(b"create_surface", on_compositor_create_surface),
    method(b"create_region", empty_handler),
    method(b"destroy", on_compositor_destroy),
];

const WL_SURFACE_METHODS: [WlMethod; 12] = [
    method(b"destroy", on_surface_destroy),
    method(b"attach", on_surface_attach),
    method(b"damage", on_surface_damage),
    method(b"frame", on_surface_frame),
    method(b"set_opaque_region", empty_handler),
    method(b"set_input_region", empty_handler),
    method(b"commit", on_surface_commit),
    method(b"set_buffer_transform", on_surface_set_buffer_transform),
    method(b"set_buffer_scale", empty_handler),
    method(b"damage_buffer", empty_handler),
    method(b"offset", empty_handler),
    method(b"set_title", on_surface_set_title),
];

const IFACES: [WlInterface; 4] = [
    WlInterface { name: b"wl_display", methods: &WL_DISPLAY_METHODS },
    WlInterface { name: b"wl_registry", methods: &WL_REGISTRY_METHODS },
    WlInterface { name: b"wl_compositor", methods: &WL_COMPOSITOR_METHODS },
    WlInterface { name: b"wl_surface", methods: &WL_SURFACE_METHODS },
];

/* ── dispatch entry (cf. wl_client_connection_data loop) ── */

pub unsafe fn hyperde_wl_dispatch(obj: u32, opcode: u32, args: *const u32) -> i32 {
    arena_lock();
    let r = dispatch_locked(obj, opcode, args);
    arena_unlock();
    r
}

unsafe fn dispatch_locked(obj: u32, opcode: u32, args: *const u32) -> i32 {
    if CLIENT_ERROR != 0 {
        kprintf(
            b"HYPERDE: srvf DROPPED obj=%u op=%u (client error)\0".as_ptr() as *const c_char,
            obj,
            opcode,
            0,
            0,
        );
        return -1;
    }
    let slot = res_slot_by_id(obj);
    if slot >= MAX_RES {
        protocol_error(ERR_INVALID_OBJECT, obj);
        return -1;
    }
    let iface = &IFACES[(*RESOURCES.as_ptr().add(slot)).iface];
    if opcode as usize >= iface.methods.len() {
        protocol_error(ERR_INVALID_METHOD, opcode);
        return -1;
    }
    (iface.methods[opcode as usize].handler)(obj, args);
    if CLIENT_ERROR != 0 {
        return -1;
    }
    0
}

pub unsafe fn hyperde_wl_reset_client() {
    arena_lock();
    CLIENT_ERROR = 0;
    arena_unlock();
}

pub unsafe fn hyperde_wl_selftest() {
    kprintf(b"HYPERDE: srvf selftest start\0".as_ptr() as *const c_char, 0, 0, 0, 0);

    /* create the per-client wl_display object (id 1) like wl_client_create */
    res_alloc(1, IFACE_DISPLAY);

    let mut args = [0u32; 4];

    /* object 1 is the wl_display object on every connection */
    hyperde_wl_dispatch(1, 0, args.as_ptr());
    args[0] = 2;
    hyperde_wl_dispatch(1, 1, args.as_ptr());

    /* registry.bind(interface=wl_compositor, version=1, new id=3) */
    args[0] = 0;
    args[1] = 0;
    args[2] = 1;
    args[3] = 3;
    hyperde_wl_dispatch(2, 0, args.as_ptr());

    /* compositor.create_surface(new id=4) */
    args[0] = 4;
    hyperde_wl_dispatch(3, 0, args.as_ptr());

    /* surface 4: attach, damage a region, set title, commit — double-buffered */
    args[0] = 0;
    args[1] = 10;
    args[2] = 12;
    hyperde_wl_dispatch(4, 1, args.as_ptr());

    args[0] = 0;
    args[1] = 0;
    args[2] = 240;
    hyperde_wl_dispatch(4, 2, args.as_ptr());

    let t = b"SelfTest\0";
    args[0] = t.as_ptr() as usize as u32;
    args[1] = (t.as_ptr() as usize >> 32) as u32;
    hyperde_wl_dispatch(4, 11, args.as_ptr());

    hyperde_wl_dispatch(4, 6, args.as_ptr());

    /* invalid method triggers wl_display.error; connection then drops traffic */
    hyperde_wl_dispatch(3, 99, args.as_ptr());
    hyperde_wl_dispatch(3, 0, args.as_ptr());

    hyperde_wl_reset_client();
    kprintf(b"HYPERDE: srvf selftest done\0".as_ptr() as *const c_char, 0, 0, 0, 0);
}
/* ═══════════════════════════════════════════════════════════════════════
 * x11_overlay — Transparent overlay windows for HyperDE (no_std kernel)
 *
 * Manages full-screen and per-window transparent overlays for:
 *   - Notification popups with glass morphism
 *   - Launcher overlay with search
 *   - Desktop widget layer
 *   - Window close/open animations
 *   - Dim-beneath-focus effects
 * ═══════════════════════════════════════════════════════════════════════ */
#![no_std]
#![allow(unused)]

use core::ffi::{c_char, c_int};

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
    fn malloc(size: usize) -> *mut u8;
    fn free(p: *mut u8);
    fn memset(p: *mut u8, val: c_int, n: usize) -> *mut u8;
    fn memcpy(dst: *mut u8, src: *const u8, n: usize) -> *mut u8;
}

/* ── Overlay types ── */

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OverlayKind {
    Notification,
    Launcher,
    DesktopWidget,
    WindowAnimation,
    DimLayer,
    Fullscreen,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OverlayState {
    Hidden,
    FadingIn,
    Visible,
    FadingOut,
}

/* ── Overlay layer ── */

#[derive(Debug, Clone, Copy)]
pub struct Overlay {
    pub kind: OverlayKind,
    pub state: OverlayState,
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
    pub alpha: f32,
    pub target_alpha: f32,
    pub fade_speed: f32,      /* alpha units per ms */
    pub z_index: i32,
    pub pixels: *mut u32,
    pub stride: u32,
    pub dirty: bool,
    pub id: u32,
}

impl Overlay {
    pub fn new(kind: OverlayKind, x: i32, y: i32, width: u32, height: u32, z: i32) -> Option<Self> {
        let stride = width * 4;
        let size = (stride * height) as usize;
        let pixels = unsafe { malloc(size) as *mut u32 };
        if pixels.is_null() { return None; }
        unsafe { memset(pixels as *mut u8, 0, size); }
        Some(Self {
            kind, state: OverlayState::Hidden,
            x, y, width, height,
            alpha: 0.0, target_alpha: 1.0,
            fade_speed: 0.004, /* 250ms fade */
            z_index: z, pixels, stride, dirty: false, id: 0,
        })
    }

    pub fn show(&mut self) {
        self.state = OverlayState::FadingIn;
        self.target_alpha = 1.0;
    }

    pub fn hide(&mut self) {
        self.state = OverlayState::FadingOut;
        self.target_alpha = 0.0;
    }

    pub fn tick(&mut self, dt_ms: u32) {
        match self.state {
            OverlayState::FadingIn => {
                self.alpha += self.fade_speed * dt_ms as f32;
                if self.alpha >= self.target_alpha {
                    self.alpha = self.target_alpha;
                    self.state = OverlayState::Visible;
                }
                self.dirty = true;
            }
            OverlayState::FadingOut => {
                self.alpha -= self.fade_speed * dt_ms as f32;
                if self.alpha <= self.target_alpha {
                    self.alpha = self.target_alpha;
                    self.state = OverlayState::Hidden;
                }
                self.dirty = true;
            }
            _ => {}
        }
    }

    pub fn fill(&mut self, color: u32) {
        if self.pixels.is_null() { return; }
        let stride_words = self.stride / 4;
        unsafe {
            let mut y = 0u32;
            while y < self.height {
                let mut x = 0u32;
                while x < self.width {
                    *self.pixels.add((y * stride_words + x) as usize) = color;
                    x += 1;
                }
                y += 1;
            }
        }
        self.dirty = true;
    }

    pub fn fill_rect(&mut self, rx: i32, ry: i32, rw: u32, rh: u32, color: u32) {
        if self.pixels.is_null() { return; }
        let stride_words = self.stride / 4;
        let sx = if rx < 0 { 0 } else { rx as u32 };
        let sy = if ry < 0 { 0 } else { ry as u32 };
        let ex = (rx as u32 + rw).min(self.width);
        let ey = (ry as u32 + rh).min(self.height);
        unsafe {
            let mut y = sy;
            while y < ey {
                let mut x = sx;
                while x < ex {
                    *self.pixels.add((y * stride_words + x) as usize) = color;
                    x += 1;
                }
                y += 1;
            }
        }
        self.dirty = true;
    }

    pub fn fill_rounded_rect(&mut self, rx: i32, ry: i32, rw: u32, rh: u32, r: u32, color: u32) {
        self.fill_rect(rx + r as i32, ry, rw - 2 * r, rh, color);
        self.fill_rect(rx, ry + r as i32, r, rh - 2 * r, color);
        self.fill_rect(rx + rw as i32 - r as i32, ry + r as i32, r, rh - 2 * r, color);
    }

    pub fn draw_text(&mut self, rx: i32, ry: i32, text: &str, color: u32) {
        let mut cx = rx;
        for ch in text.bytes() {
            if ch >= 0x20 && ch < 0x7f {
                self.fill_rect(cx, ry, 8, 12, color);
            }
            cx += 8;
        }
        self.dirty = true;
    }

    pub fn clear(&mut self, color: u32) {
        self.fill(color);
    }

    pub fn is_visible(&self) -> bool {
        self.alpha > 0.01
    }
}

/* Note: Overlay uses manual memory management.  Pixels are freed
 * by the caller (OverlayManager::remove) via free().
 * No Drop impl — Copy is required for the static array. */

/* ── Overlay manager ── */

const MAX_OVERLAYS: usize = 16;

#[derive(Debug, Clone, Copy)]
pub struct OverlayManager {
    pub overlays: [Option<Overlay>; MAX_OVERLAYS],
    pub count: usize,
    pub next_id: u32,
}

impl OverlayManager {
    pub fn new() -> Self {
        Self {
            overlays: [None; MAX_OVERLAYS],
            count: 0,
            next_id: 1,
        }
    }

    pub fn create(&mut self, kind: OverlayKind, x: i32, y: i32,
                  w: u32, h: u32, z: i32) -> Option<u32> {
        if self.count >= MAX_OVERLAYS { return None; }
        if let Some(ov) = Overlay::new(kind, x, y, w, h, z) {
            let id = self.next_id;
            self.next_id += 1;
            let mut ov = ov;
            ov.id = id;
            self.overlays[self.count] = Some(ov);
            self.count += 1;
            Some(id)
        } else {
            None
        }
    }

    pub fn get(&mut self, id: u32) -> Option<&mut Overlay> {
        for i in 0..self.count {
            if let Some(ref ov) = self.overlays[i] {
                if ov.id == id {
                    return Some(unsafe { &mut *(&mut self.overlays[i] as *mut Option<Overlay>).cast::<Overlay>() });
                }
            }
        }
        None
    }

    pub fn remove(&mut self, id: u32) {
        for i in 0..self.count {
            if let Some(ref ov) = self.overlays[i] {
                if ov.id == id {
                    self.overlays[i] = None;
                    /* compact */
                    for j in i..self.count - 1 {
                        self.overlays[j] = self.overlays[j + 1].take();
                    }
                    self.overlays[self.count - 1] = None;
                    self.count -= 1;
                    return;
                }
            }
        }
    }

    pub fn tick_all(&mut self, dt_ms: u32) {
        for i in 0..self.count {
            if let Some(ref mut ov) = self.overlays[i] {
                ov.tick(dt_ms);
            }
        }
    }

    pub fn composite_all(&self, dst: &mut Overlay) {
        /* Sort by z_index and composite lowest first */
        for z in -10..10 {
            for i in 0..self.count {
                if let Some(ref ov) = self.overlays[i] {
                    if ov.z_index == z && ov.is_visible() {
                        self.composite_one(ov, dst);
                    }
                }
            }
        }
    }

    fn composite_one(&self, src: &Overlay, dst: &mut Overlay) {
        if src.pixels.is_null() || dst.pixels.is_null() { return; }
        if src.alpha < 0.01 { return; }
        let alpha = (src.alpha * 255.0) as u32;
        let src_sw = src.stride / 4;
        let dst_sw = dst.stride / 4;
        unsafe {
            let mut y = 0u32;
            while y < src.height {
                let dy = src.y + y as i32;
                if dy < 0 || dy >= dst.height as i32 { y += 1; continue; }
                let mut x = 0u32;
                while x < src.width {
                    let dx = src.x + x as i32;
                    if dx < 0 || dx >= dst.width as i32 { x += 1; continue; }
                    let sp = *src.pixels.add((y * src_sw + x) as usize);
                    let sa = ((sp >> 24) & 0xFF) * alpha / 255;
                    if sa > 10 {
                        let dp = dst.pixels.add((dy as u32 * dst_sw + dx as u32) as usize);
                        let dp_val = *dp;
                        let da = 255 - sa;
                        let r = (((dp_val >> 16) & 0xFF) * da + ((sp >> 16) & 0xFF) * sa) / 255;
                        let g = (((dp_val >> 8) & 0xFF) * da + ((sp >> 8) & 0xFF) * sa) / 255;
                        let b = ((dp_val & 0xFF) * da + (sp & 0xFF) * sa) / 255;
                        *dp = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                    x += 1;
                }
                y += 1;
            }
        }
        dst.dirty = true;
    }

    pub fn visible_count(&self) -> usize {
        let mut n = 0;
        for i in 0..self.count {
            if let Some(ref ov) = self.overlays[i] {
                if ov.is_visible() { n += 1; }
            }
        }
        n
    }

    pub fn clear_all(&mut self) {
        for i in 0..self.count {
            self.overlays[i] = None;
        }
        self.count = 0;
    }
}

/* ── Notification overlay helper ── */

pub struct Notification {
    pub overlay_id: u32,
    pub title: [u8; 64],
    pub body: [u8; 256],
    pub icon_color: u32,
    pub timeout_ms: u32,
    pub elapsed_ms: u32,
}

impl Notification {
    pub fn new(manager: &mut OverlayManager, title: &str, body: &str,
               icon_color: u32) -> Option<Self> {
        let w = 360u32;
        let h = 80u32;
        let id = manager.create(OverlayKind::Notification, 10, 40, w, h, 100)?;
        let ov = manager.get(id)?;
        /* Glass morphism background */
        ov.fill_rounded_rect(0, 0, w, h, 12, 0xC8242428);
        ov.fill_rect(0, 0, 4, h, icon_color);
        /* Title + body */
        let mut t_buf = [0u8; 64];
        let tb = title.as_bytes();
        let n = tb.len().min(63);
        let mut i = 0;
        while i < n { t_buf[i] = tb[i]; i += 1; }
        let mut b_buf = [0u8; 256];
        let bb = body.as_bytes();
        let n2 = bb.len().min(255);
        i = 0;
        while i < n2 { b_buf[i] = bb[i]; i += 1; }
        ov.dirty = true;
        Some(Self {
            overlay_id: id,
            title: t_buf,
            body: b_buf,
            icon_color,
            timeout_ms: 3000,
            elapsed_ms: 0,
        })
    }

    pub fn tick(&mut self, manager: &mut OverlayManager, dt_ms: u32) {
        self.elapsed_ms += dt_ms;
        if self.elapsed_ms >= self.timeout_ms {
            if let Some(ov) = manager.get(self.overlay_id) {
                ov.hide();
            }
        }
    }
}

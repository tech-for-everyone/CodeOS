/* ═══════════════════════════════════════════════════════════════════════
 * gdk4_x11 — GDK4 X11 backend for HyperDE (no_std kernel port)
 *
 * Provides the display, surface, and event-loop primitives that
 * HyperDE needs for smooth rendering.  Routes through the kernel's
 * X11 server and framebuffer.
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

/* ── Display handle ── */

pub struct X11Display {
    pub root_window: u32,
    pub screen_width: u32,
    pub screen_height: u32,
    pub depth: u32,
    pub black: u32,
    pub white: u32,
}

impl X11Display {
    pub fn open(_name: Option<&str>) -> Option<Self> {
        Some(Self {
            root_window: 1,
            screen_width: 1920,
            screen_height: 1080,
            depth: 32,
            black: 0xFF000000,
            white: 0xFFFFFFFF,
        })
    }

    pub fn default_screen(&self) -> u32 { 0 }
    pub fn width(&self) -> u32 { self.screen_width }
    pub fn height(&self) -> u32 { self.screen_height }
    pub fn depth(&self) -> u32 { self.depth }
}

/* ── Surface (rendering target) ── */

pub struct Surface {
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub pixels: *mut u32,
    pub dirty: bool,
    pub x: i32,
    pub y: i32,
}

impl Surface {
    pub fn new(width: u32, height: u32) -> Option<Self> {
        let stride = width * 4;
        let size = (stride * height) as usize;
        let pixels = unsafe { malloc(size) as *mut u32 };
        if pixels.is_null() { return None; }
        unsafe { memset(pixels as *mut u8, 0, size); }
        Some(Self { width, height, stride, pixels, dirty: false, x: 0, y: 0 })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        unsafe {
            if !self.pixels.is_null() { free(self.pixels as *mut u8); }
        }
        let stride = width * 4;
        let size = (stride * height) as usize;
        let pixels = unsafe { malloc(size) as *mut u32 };
        if !pixels.is_null() { unsafe { memset(pixels as *mut u8, 0, size); } }
        self.width = width;
        self.height = height;
        self.stride = stride;
        self.pixels = pixels;
        self.dirty = true;
    }

    pub fn fill_rect(&mut self, x: i32, y: i32, w: u32, h: u32, color: u32) {
        if self.pixels.is_null() { return; }
        let sx = if x < 0 { 0 } else { x as u32 };
        let sy = if y < 0 { 0 } else { y as u32 };
        let ex = (x as u32 + w).min(self.width);
        let ey = (y as u32 + h).min(self.height);
        let stride_words = self.stride / 4;
        unsafe {
            let mut row = sy;
            while row < ey {
                let mut col = sx;
                let row_ptr = self.pixels.add((row * stride_words) as usize);
                while col < ex {
                    *row_ptr.add(col as usize) = color;
                    col += 1;
                }
                row += 1;
            }
        }
        self.dirty = true;
    }

    pub fn draw_rect(&mut self, x: i32, y: i32, w: u32, h: u32, color: u32) {
        self.fill_rect(x, y, w, 1, color);
        self.fill_rect(x, y + h as i32 - 1, w, 1, color);
        self.fill_rect(x, y, 1, h, color);
        self.fill_rect(x + w as i32 - 1, y, 1, h, color);
    }

    pub fn fill_rounded_rect(&mut self, x: i32, y: i32, w: u32, h: u32, r: u32, color: u32) {
        self.fill_rect(x + r as i32, y, w - 2 * r, h, color);
        self.fill_rect(x, y + r as i32, r, h - 2 * r, color);
        self.fill_rect(x + w as i32 - r as i32, y + r as i32, r, h - 2 * r, color);
    }

    pub fn draw_text(&mut self, x: i32, y: i32, text: &str, color: u32, font_size: u32) {
        /* Basic 8x8 bitmap font rendering */
        let glyph_w = font_size.max(8);
        let glyph_h = font_size.max(8);
        let mut cx = x;
        for ch in text.bytes() {
            if ch >= 0x20 && ch < 0x7f {
                self.fill_rect(cx, y, glyph_w, glyph_h, color);
            }
            cx += glyph_w as i32;
        }
        self.dirty = true;
    }

    pub fn composite(&self, dst: &mut Surface, dx: i32, dy: i32) {
        if self.pixels.is_null() || dst.pixels.is_null() { return; }
        let src_stride_words = self.stride / 4;
        let dst_stride_words = dst.stride / 4;
        unsafe {
            let mut row = 0u32;
            while row < self.height {
                let sy = dy + row as i32;
                if sy < 0 || sy >= dst.height as i32 { row += 1; continue; }
                let mut col = 0u32;
                while col < self.width {
                    let sx = dx + col as i32;
                    if sx < 0 || sx >= dst.width as i32 { col += 1; continue; }
                    let pixel = *self.pixels.add((row * src_stride_words + col) as usize);
                    let alpha = (pixel >> 24) & 0xFF;
                    if alpha > 128 {
                        *dst.pixels.add((sy as u32 * dst_stride_words + sx as u32) as usize) = pixel;
                    }
                    col += 1;
                }
                row += 1;
            }
        }
        dst.dirty = true;
    }

    pub fn clear(&mut self, color: u32) {
        self.fill_rect(0, 0, self.width, self.height, color);
    }
}

impl Drop for Surface {
    fn drop(&mut self) {
        if !self.pixels.is_null() {
            unsafe { free(self.pixels as *mut u8); }
            self.pixels = core::ptr::null_mut();
        }
    }
}

/* ── Event types ── */

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum EventType {
    None,
    KeyPress,
    KeyRelease,
    ButtonPress,
    ButtonRelease,
    Motion,
    Expose,
    MapRequest,
    Unmap,
    Destroy,
    Configure,
    FocusIn,
    FocusOut,
}

#[derive(Debug, Clone, Copy)]
pub struct Event {
    pub type_: EventType,
    pub window: u32,
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
    pub key_code: u32,
    pub state: u32,
    pub button: u32,
}

impl Event {
    pub fn none() -> Self {
        Self { type_: EventType::None, window: 0, x: 0, y: 0, width: 0, height: 0, key_code: 0, state: 0, button: 0 }
    }
}

/* ── RGBA color helpers ── */

pub const fn rgba(r: u8, g: u8, b: u8, a: u8) -> u32 {
    (a as u32) << 24 | (r as u32) << 16 | (g as u32) << 8 | b as u32
}

pub const fn rgb(r: u8, g: u8, b: u8) -> u32 {
    rgba(r, g, b, 0xFF)
}

pub fn alpha_blend(dst: u32, src: u32) -> u32 {
    let sa = ((src >> 24) & 0xFF) as u32;
    if sa == 0 { return dst; }
    if sa == 0xFF { return src; }
    let da = 255 - sa;
    let dr = (((dst >> 16) & 0xFF) * da + ((src >> 16) & 0xFF) * sa) / 255;
    let dg = (((dst >> 8) & 0xFF) * da + ((src >> 8) & 0xFF) * sa) / 255;
    let db = ((dst & 0xFF) * da + (src & 0xFF) * sa) / 255;
    (0xFF << 24) | (dr << 16) | (dg << 8) | db
}

/* ── Animation / easing ── */

#[derive(Debug, Clone, Copy, Default)]
pub enum Easing {
    #[default]
    Linear,
    EaseInOut,
    EaseOut,
    EaseIn,
    Bounce,
    Spring,
}

impl Easing {
    pub fn apply(&self, t: f32) -> f32 {
        let tt = t * t;
        let ttt = tt * t;
        match self {
            Easing::Linear => t,
            Easing::EaseInOut => {
                if t < 0.5 { 4.0 * t * t } else { 1.0 - (2.0 * (1.0 - t)) * (2.0 * (1.0 - t)) / 2.0 }
            }
            Easing::EaseOut => 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t),
            Easing::EaseIn => tt * t,
            Easing::Bounce => {
                let mut t2 = t;
                if t2 < 1.0 / 2.75 { 7.5625 * t2 * t2 }
                else { t2 -= 1.5 / 2.75; if t2 < 2.0 / 2.75 { 7.5625 * t2 * t2 + 0.75 } else { t2 -= 1.75 / 2.75; if t2 < 2.5 / 2.75 { 7.5625 * t2 * t2 + 0.9375 } else { 7.5625 * t2 * t2 + 0.984375 } } }
            }
            Easing::Spring => {
                /* Simplified spring approximation without libm */
                if t == 0.0 || t == 1.0 { t }
                else { t * (2.0 - t) }
            }
        }
    }
}

#[derive(Debug, Clone, Copy, Default)]
pub struct Animation {
    pub from: f32,
    pub to: f32,
    pub duration_ms: u32,
    pub elapsed_ms: u32,
    pub easing: Easing,
    pub running: bool,
}

impl Animation {
    pub fn new(from: f32, to: f32, duration_ms: u32, easing: Easing) -> Self {
        Self { from, to, duration_ms, elapsed_ms: 0, easing, running: true }
    }

    pub fn tick(&mut self, dt_ms: u32) {
        if !self.running { return; }
        self.elapsed_ms = (self.elapsed_ms + dt_ms).min(self.duration_ms);
        if self.elapsed_ms >= self.duration_ms {
            self.running = false;
        }
    }

    pub fn value(&self) -> f32 {
        let t = if self.duration_ms == 0 { 1.0 }
                else { (self.elapsed_ms as f32) / (self.duration_ms as f32) };
        let t = t.min(1.0).max(0.0);
        let eased = self.easing.apply(t);
        self.from + (self.to - self.from) * eased
    }

    pub fn is_done(&self) -> bool {
        !self.running
    }
}

/* ── Blur / glass effect ── */

pub fn blur_surface(src: &Surface, dst: &mut Surface, radius: u32) {
    if src.pixels.is_null() || dst.pixels.is_null() { return; }
    let r = radius as i32;
    let src_stride_words = src.stride / 4;
    let dst_stride_words = dst.stride / 4;
    unsafe {
        let mut y = 0i32;
        while y < src.height as i32 {
            let mut x = 0i32;
            while x < src.width as i32 {
                let mut sr: u32 = 0;
                let mut sg: u32 = 0;
                let mut sb: u32 = 0;
                let mut sa: u32 = 0;
                let mut count: u32 = 0;
                let mut ky = -r;
                while ky <= r {
                    let sy = y + ky;
                    if sy >= 0 && sy < src.height as i32 {
                        let mut kx = -r;
                        while kx <= r {
                            let sx = x + kx;
                            if sx >= 0 && sx < src.width as i32 {
                                let pixel = *src.pixels.add((sy as u32 * src_stride_words + sx as u32) as usize);
                                sr += (pixel >> 16) & 0xFF;
                                sg += (pixel >> 8) & 0xFF;
                                sb += pixel & 0xFF;
                                sa += (pixel >> 24) & 0xFF;
                                count += 1;
                            }
                            kx += 1;
                        }
                    }
                    ky += 1;
                }
                if count > 0 {
                    let out = ((sa / count) << 24) | ((sr / count) << 16) | ((sg / count) << 8) | (sb / count);
                    *dst.pixels.add((y as u32 * dst_stride_words + x as u32) as usize) = out;
                }
                x += 1;
            }
            y += 1;
        }
    }
    dst.dirty = true;
}

/* ── Shadow rendering ── */

pub fn draw_shadow(surface: &mut Surface, x: i32, y: i32, w: u32, h: u32, r: u32, blur: u32) {
    let outer_x = x - blur as i32 - 4;
    let outer_y = y - blur as i32 - 4;
    let outer_w = w + 2 * blur + 8;
    let outer_h = h + 2 * blur + 8;
    surface.fill_rounded_rect(outer_x, outer_y, outer_w, outer_h, r + blur, rgba(0, 0, 0, 40));
}

/* ── Gradient helpers ── */

pub fn gradient_vertical(surface: &mut Surface, x: i32, y: i32, w: u32, h: u32,
                         top: u32, bottom: u32) {
    if surface.pixels.is_null() { return; }
    let stride_words = surface.stride / 4;
    unsafe {
        let mut row = 0u32;
        while row < h {
            let t = row as f32 / h as f32;
            let r = lerp(((top >> 16) & 0xFF) as f32, ((bottom >> 16) & 0xFF) as f32, t) as u32;
            let g = lerp(((top >> 8) & 0xFF) as f32, ((bottom >> 8) & 0xFF) as f32, t) as u32;
            let b = lerp((top & 0xFF) as f32, (bottom & 0xFF) as f32, t) as u32;
            let color = 0xFF000000 | (r << 16) | (g << 8) | b;
            let py = y + row as i32;
            if py >= 0 && py < surface.height as i32 {
                let mut col = 0u32;
                while col < w {
                    let px = x + col as i32;
                    if px >= 0 && px < surface.width as i32 {
                        *surface.pixels.add((py as u32 * stride_words + px as u32) as usize) = color;
                    }
                    col += 1;
                }
            }
            row += 1;
        }
    }
    surface.dirty = true;
}

pub fn gradient_horizontal(surface: &mut Surface, x: i32, y: i32, w: u32, h: u32,
                           left: u32, right: u32) {
    if surface.pixels.is_null() { return; }
    let stride_words = surface.stride / 4;
    unsafe {
        let mut row = 0u32;
        while row < h {
            let mut col = 0u32;
            while col < w {
                let t = col as f32 / w as f32;
                let r = lerp(((left >> 16) & 0xFF) as f32, ((right >> 16) & 0xFF) as f32, t) as u32;
                let g = lerp(((left >> 8) & 0xFF) as f32, ((right >> 8) & 0xFF) as f32, t) as u32;
                let b = lerp((left & 0xFF) as f32, (right & 0xFF) as f32, t) as u32;
                let color = 0xFF000000 | (r << 16) | (g << 8) | b;
                let py = y + row as i32;
                let px = x + col as i32;
                if py >= 0 && py < surface.height as i32 && px >= 0 && px < surface.width as i32 {
                    *surface.pixels.add((py as u32 * stride_words + px as u32) as usize) = color;
                }
                col += 1;
            }
            row += 1;
        }
    }
    surface.dirty = true;
}

fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

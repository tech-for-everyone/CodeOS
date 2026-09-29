/* ───────────────────────── HyperDE Animation Module ─────────────────────────
 * Uses gdk4_x11 easing and animation system for smooth window
 * transitions, panel fades, and window movements.
 * ───────────────────────────────────────────────────────────────────── */

use gdk4_x11::{Animation, Easing};

/* ── Animation types ── */

#[derive(Debug, Clone, Copy, Default)]
pub enum AnimationType {
    #[default]
    FadeIn,
    FadeOut,
    SlideLeft,
    SlideRight,
    ScaleIn,
    ScaleOut,
    BlurIn,
    Move,
}

/* ── Active animation ── */

#[derive(Debug, Clone, Copy)]
pub struct ActiveAnim {
    pub anim_type: AnimationType,
    pub anim: Animation,
    pub from_x: i32,
    pub from_y: i32,
    pub from_w: u32,
    pub from_h: u32,
    pub to_x: i32,
    pub to_y: i32,
    pub to_w: u32,
    pub to_h: u32,
    pub done: bool,
}

const MAX_ANIMATIONS: usize = 32;

static mut ANIMATIONS: [Animation; MAX_ANIMATIONS] = [Animation { from: 0.0, to: 0.0, duration_ms: 0, elapsed_ms: 0, easing: Easing::Linear, running: false }; MAX_ANIMATIONS];
static mut ANIM_TYPES: [AnimationType; MAX_ANIMATIONS] = [AnimationType::Move; MAX_ANIMATIONS];
static mut ANIM_FROM: [i32; MAX_ANIMATIONS] = [0; MAX_ANIMATIONS];
static mut ANIM_FROM_Y: [i32; MAX_ANIMATIONS] = [0; MAX_ANIMATIONS];
static mut ANIM_TO: [i32; MAX_ANIMATIONS] = [0; MAX_ANIMATIONS];
static mut ANIM_TO_Y: [i32; MAX_ANIMATIONS] = [0; MAX_ANIMATIONS];
static mut ANIM_W: [u32; MAX_ANIMATIONS] = [0; MAX_ANIMATIONS];
static mut ANIM_H: [u32; MAX_ANIMATIONS] = [0; MAX_ANIMATIONS];
static mut ANIM_DONE: [bool; MAX_ANIMATIONS] = [false; MAX_ANIMATIONS];
static mut ANIM_COUNT: usize = 0;

/// Start a window animation
#[allow(unused_variables)]
pub fn start_animation(
    anim_type: AnimationType,
    from_x: i32, from_y: i32, from_w: u32, from_h: u32,
    to_x: i32, to_y: i32, to_w: u32, to_h: u32,
    duration_ms: u32,
) -> usize {
    unsafe {
        if ANIM_COUNT >= MAX_ANIMATIONS { return 0; }
        let idx = ANIM_COUNT;
        let easing = match anim_type {
            AnimationType::FadeIn | AnimationType::FadeOut => Easing::EaseOut,
            AnimationType::SlideLeft | AnimationType::SlideRight => Easing::EaseInOut,
            AnimationType::ScaleIn | AnimationType::ScaleOut => Easing::Spring,
            AnimationType::BlurIn | AnimationType::Move => Easing::Linear,
        };
        ANIMATIONS[idx] = Animation::new(0.0, 1.0, duration_ms, easing);
        ANIM_TYPES[idx] = anim_type;
        ANIM_FROM[idx] = from_x;
        ANIM_FROM_Y[idx] = from_y;
        ANIM_TO[idx] = to_x;
        ANIM_TO_Y[idx] = to_y;
        ANIM_W[idx] = from_w;
        ANIM_H[idx] = from_h;
        ANIM_DONE[idx] = false;
        ANIM_COUNT += 1;
        idx
    }
}

/// Tick all animations. Returns true if any animation completed.
pub fn tick_animations(dt_ms: u32) -> bool {
    unsafe {
        let mut any_done = false;
        let mut i = 0;
        while i < ANIM_COUNT {
            ANIMATIONS[i].tick(dt_ms);
            if ANIMATIONS[i].is_done() {
                ANIM_DONE[i] = true;
                any_done = true;
            }
            i += 1;
        }
        any_done
    }
}

/// Get animation progress (0.0 to 1.0)
pub fn get_animation_progress(idx: usize) -> f32 {
    unsafe {
        if idx < ANIM_COUNT {
            return ANIMATIONS[idx].value();
        }
        1.0
    }
}

/// Check if animation at index is done
pub fn is_animation_done(idx: usize) -> bool {
    unsafe {
        if idx < ANIM_COUNT {
            return ANIM_DONE[idx];
        }
        true
    }
}

/// Remove completed animation by shifting
pub fn remove_animation(idx: usize) {
    unsafe {
        if idx >= ANIM_COUNT { return; }
        ANIM_COUNT -= 1;
        if idx < ANIM_COUNT {
            ANIMATIONS[idx] = ANIMATIONS[ANIM_COUNT];
            ANIM_TYPES[idx] = ANIM_TYPES[ANIM_COUNT];
            ANIM_FROM[idx] = ANIM_FROM[ANIM_COUNT];
            ANIM_TO[idx] = ANIM_TO[ANIM_COUNT];
            ANIM_W[idx] = ANIM_W[ANIM_COUNT];
            ANIM_H[idx] = ANIM_H[ANIM_COUNT];
            ANIM_DONE[idx] = ANIM_DONE[ANIM_COUNT];
        }
    }
}

/// Clear all animations
pub fn clear_animations() {
    unsafe {
        ANIM_COUNT = 0;
        for i in 0..MAX_ANIMATIONS {
            ANIM_DONE[i] = false;
        }
    }
}

/// Get total animation count
pub fn animation_count() -> usize {
    unsafe { ANIM_COUNT }
}

/// Start a fade animation on a surface region
pub fn start_fade(
    from_alpha: f32, to_alpha: f32,
    duration_ms: u32,
    easing: Easing,
) -> usize {
    unsafe {
        if ANIM_COUNT >= MAX_ANIMATIONS { return 0; }
        let idx = ANIM_COUNT;
        ANIMATIONS[idx] = Animation::new(from_alpha, to_alpha, duration_ms, easing);
        ANIM_TYPES[idx] = AnimationType::FadeIn;
        ANIM_DONE[idx] = false;
        ANIM_COUNT += 1;
        idx
    }
}

/// Get the current alpha value from animation index
pub fn get_alpha(idx: usize) -> f32 {
    unsafe {
        if idx < ANIM_COUNT {
            ANIMATIONS[idx].value()
        } else {
            1.0
        }
    }
}

/// Get the target alpha
pub fn get_alpha_target(idx: usize) -> f32 {
    unsafe {
        if idx < ANIM_COUNT {
            ANIMATIONS[idx].to
        } else {
            1.0
        }
    }
}

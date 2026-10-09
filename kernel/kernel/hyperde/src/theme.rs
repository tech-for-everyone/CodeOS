/* ───────────────────────── HyperDE Theme Module ─────────────────────────
 * Provides theme configuration loaded via mlua conf.lua.
 * Supports dynamic theme switching and color palettes.
 * ───────────────────────────────────────────────────────────────────── */

use crate::config::{config_get, config_get_number, config_get_string};

#[derive(Debug, Clone, Copy)]
pub struct ThemeColors {
    pub accent: u32,
    pub background: u32,
    pub foreground: u32,
    pub surface: u32,
    pub surface_variant: u32,
    pub border: u32,
    pub error: u32,
    pub warning: u32,
    pub success: u32,
    pub panel_bg: u32,
    pub panel_fg: u32,
    pub glass_top: u32,
    pub glass_bottom: u32,
    pub glass_alpha: u8,
}

impl ThemeColors {
    pub const DEFAULT: Self = Self {
        accent: 0x0040D9F0,
        background: 0xFF1a1a2e,
        foreground: 0xFFe0e0e0,
        surface: 0xFF16213e,
        surface_variant: 0xFF0f3460,
        border: 0xFF2a2a4a,
        error: 0xFFe94560,
        warning: 0xFFffc107,
        success: 0xFF4caf50,
        panel_bg: 0xFF1e1e2e,
        panel_fg: 0xFFcdd6f4,
        glass_top: 0xFF242428,
        glass_bottom: 0xFF17171B,
        glass_alpha: 0xC8,
    };
}

/* ── Theme manager ── */

const MAX_THEMES: usize = 8;
static mut THEMES: [ThemeColors; MAX_THEMES] = [ThemeColors::DEFAULT; MAX_THEMES];
static mut THEME_COUNT: usize = 1; /* At least one default */
static mut ACTIVE_THEME: usize = 0;

pub fn active_theme() -> &'static ThemeColors {
    unsafe { &THEMES[ACTIVE_THEME] }
}

/// Load theme from mlua config
pub fn load_theme_from_config() {
    unsafe {
        let colors = &mut THEMES[0];
        colors.accent = config_get_number(0, "theme.accent", 0.0) as u32;
        colors.background = config_get_number(0, "theme.background", 0.0) as u32;
        colors.panel_bg = config_get_number(0, "theme.panel_background", 0.0) as u32;
        colors.panel_fg = config_get_number(0, "theme.panel_foreground", 0.0) as u32;
        colors.glass_top = config_get_number(0, "theme.glass_top", 0.0) as u32;
        colors.glass_bottom = config_get_number(0, "theme.glass_bottom", 0.0) as u32;

        /* Parse glass alpha if present */
        let glass_alpha = config_get_string(0, "theme.glass_alpha");
        if glass_alpha.len() == 2 {
            colors.glass_alpha = glass_alpha[0] as u8;
        }
        /* Parse accent color string */
        let accent_str = config_get_string(0, "theme.accent");
        if accent_str.len() >= 7 && accent_str[0] == b'#' {
            colors.accent = parse_hex(accent_str);
        }
    }
}

fn parse_hex(s: &[u8]) -> u32 {
    let mut val: u32 = 0;
    let start = if s[0] == b'#' { 1 } else { 0 };
    let mut i = start;
    while i < s.len() && i < start + 6 {
        let c = s[i];
        let h = match c {
            b'0'..=b'9' => c - b'0',
            b'a'..=b'f' => c - b'a' + 10,
            b'A'..=b'F' => c - b'A' + 10,
            _ => 0,
        };
        val = (val << 4) | h as u32;
        i += 1;
    }
    val | 0xFF000000
}

/// Apply a theme by index
pub fn apply_theme(idx: usize) {
    unsafe {
        if idx < THEME_COUNT {
            ACTIVE_THEME = idx;
        }
    }
}

/// Add a custom theme
pub fn add_theme(colors: ThemeColors) -> usize {
    unsafe {
        if THEME_COUNT < MAX_THEMES {
            THEMES[THEME_COUNT] = colors;
            THEME_COUNT += 1;
            THEME_COUNT - 1
        } else {
            0
        }
    }
}

/* ── Theme helpers for rendering ── */

pub fn glass_gradient(top_color: u32, bottom_color: u32, alpha: u8) -> u32 {
    let a = alpha as u32;
    let tr = ((top_color >> 16) & 0xFF) as u32;
    let tg = ((top_color >> 8) & 0xFF) as u32;
    let tb = (top_color & 0xFF) as u32;
    let br = ((bottom_color >> 16) & 0xFF) as u32;
    let bg = ((bottom_color >> 8) & 0xFF) as u32;
    let bb = (bottom_color & 0xFF) as u32;
    let r = (tr * a + br * (255 - a)) / 255;
    let g = (tg * a + bg * (255 - a)) / 255;
    let b = (tb * a + bb * (255 - a)) / 255;
    0xFF000000 | (r << 16) | (g << 8) | b
}

pub fn accent_color() -> u32 {
    active_theme().accent
}

pub fn panel_background() -> u32 {
    active_theme().panel_bg
}

pub fn panel_foreground() -> u32 {
    active_theme().panel_fg
}

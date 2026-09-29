/* ───────────────────────── HyperDE Configuration ─────────────────────────
 * Uses the mlua crate for modular Lua-based configuration (like Hyprland).
 * ───────────────────────────────────────────────────────────────────────── */

#![allow(unused)]

use core::ffi::c_char;
use core::sync::atomic::{AtomicBool, Ordering};

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
}

/* ── Re-exports from mlua crate ── */
pub use mlua::{execute, execute_with_requires,
               config_get, config_get_number,
               config_get_string, config_get_bool, config_get_table, table_len,
               table_get, table_get_by_index, Value};

/* ───────────────────────── Config values ───────────────────────── */

const MAX_WORKSPACES: usize = 9;
const MAX_CMD_LEN: usize = 64;
const MAX_FLOAT_CLASSES: usize = 8;
const MAX_CLASS_LEN: usize = 32;

pub struct HyperdeConfig {
    pub workspace_names: [[u8; 16]; MAX_WORKSPACES],
    pub workspace_count: usize,
    pub accent: u32,
    pub focused_border: u32,
    pub normal_border: u32,
    pub glass_top: u32,
    pub glass_bottom: u32,
    pub glass_alpha: u32,
    pub border_width: u32,
    pub focus_follow_mouse: bool,
    pub panel_height: u32,
    pub terminal_cmd: [u8; MAX_CMD_LEN],
    pub launcher_cmd: [u8; MAX_CMD_LEN],
    pub floating_classes: [[u8; MAX_CLASS_LEN]; MAX_FLOAT_CLASSES],
    pub floating_class_count: usize,
    pub animation_easing: u32,  /* 0=linear, 1=easeInOut, 2=easeOut, 3=spring */
    pub animation_duration_ms: u32,
    pub blur_enabled: bool,
    pub shadow_enabled: bool,
    pub blur_radius: u32,
    pub shadow_blur: u32,
}

impl HyperdeConfig {
    fn workspace_name_str(&self, idx: usize) -> &str {
        if idx >= self.workspace_count { return ""; }
        let name = &self.workspace_names[idx];
        let mut len = 0;
        while len < name.len() && name[len] != 0 { len += 1; }
        core::str::from_utf8(&name[..len]).unwrap_or("")
    }
}

static mut CONFIG: HyperdeConfig = HyperdeConfig {
    workspace_names: {
        let mut ws = [[0u8; 16]; MAX_WORKSPACES];
        let mut i = 0;
        while i < MAX_WORKSPACES {
            ws[i][0] = b'1' + i as u8;
            i += 1;
        }
        ws
    },
    workspace_count: 9,
    accent: 0x0040D9F0,
    focused_border: 0x0040D9F0,
    normal_border: 0x003c3836,
    glass_top: 0x00242428,
    glass_bottom: 0x0017171B,
    glass_alpha: 0xC8,
    border_width: 2,
    focus_follow_mouse: true,
    panel_height: 28,
    terminal_cmd: {
        let mut buf = [0u8; MAX_CMD_LEN];
        let src = b"xterm";
        let mut i = 0;
        while i < src.len() && i < MAX_CMD_LEN { buf[i] = src[i]; i += 1; }
        buf
    },
    launcher_cmd: [0; MAX_CMD_LEN],
    floating_classes: [[0; MAX_CLASS_LEN]; MAX_FLOAT_CLASSES],
    floating_class_count: 2,
    animation_easing: 2,  /* easeOut */
    animation_duration_ms: 200,
    blur_enabled: true,
    shadow_enabled: true,
    blur_radius: 8,
    shadow_blur: 4,
};
static CONFIG_LOADED: AtomicBool = AtomicBool::new(false);

pub fn config() -> &'static mut HyperdeConfig {
    unsafe { &mut CONFIG }
}

pub fn config_loaded() -> bool {
    CONFIG_LOADED.load(Ordering::Relaxed)
}

/* ───────────────────────── Config parsing via mlua ────────────────── */

/// Load and parse config from a Lua source buffer.
/// This is called from C with the config data.
pub fn config_parse_lua(data: &[u8]) {
    let root_idx = execute(data);
    if root_idx == 0 {
        unsafe { kprintf(b"HYPERDE CONFIG: failed to parse Lua config\0".as_ptr() as *const c_char, 0, 0, 0, 0); }
        return;
    }

    let cfg = unsafe { &mut CONFIG };

    /* Read from the parsed Lua config tables */
    cfg.border_width = config_get_number(root_idx, "window_manager.border_width", 2.0) as u32;
    cfg.focus_follow_mouse = config_get_bool(root_idx, "window_manager.focus_follow_mouse", true);
    cfg.panel_height = config_get_number(root_idx, "compositor.panel_height", 28.0) as u32;
    cfg.animation_duration_ms = config_get_number(root_idx, "animations.duration", 200.0) as u32;
    cfg.blur_enabled = config_get_bool(root_idx, "decorations.blur", true);
    cfg.shadow_enabled = config_get_bool(root_idx, "decorations.shadow", true);
    cfg.blur_radius = config_get_number(root_idx, "decorations.blur_radius", 8.0) as u32;
    cfg.shadow_blur = config_get_number(root_idx, "decorations.shadow_blur", 4.0) as u32;

    /* Colors */
    {
        let normal_border = config_get_string(root_idx, "window_manager.normal_border");
        if !normal_border.is_empty() {
            if let Some(c) = parse_hex_color(normal_border) { cfg.normal_border = c; }
        }
        let focused_border = config_get_string(root_idx, "window_manager.focused_border");
        if !focused_border.is_empty() {
            if let Some(c) = parse_hex_color(focused_border) { cfg.focused_border = c; }
        }
    }

    /* Workspace names */
    {
        let mut count = 0usize;
        let mut idx = 1u8;
        loop {
            let mut key = [0u8; 40];
            let base = b"window_manager.workspaces.";
            let mut i = 0;
            while i < base.len() { key[i] = base[i]; i += 1; }
            key[i] = idx; i += 1;
            let s = config_get_string(root_idx, core::str::from_utf8(&key[..i]).unwrap());
            if !s.is_empty() {
                let n = s.len().min(15);
                let mut j = 0;
                while j < n { cfg.workspace_names[count][j] = s[j]; j += 1; }
                cfg.workspace_names[count][n] = 0;
                count += 1;
            } else { break; }
            idx += 1;
            if count >= MAX_WORKSPACES { break; }
        }
        if count > 0 { cfg.workspace_count = count; }
    }

    /* Floating classes */
    {
        let mut count = 0usize;
        let mut idx = 1u8;
        loop {
            let mut key = [0u8; 40];
            let base = b"window_manager.floating_classes.";
            let mut i = 0;
            while i < base.len() { key[i] = base[i]; i += 1; }
            key[i] = idx; i += 1;
            let s = config_get_string(root_idx, core::str::from_utf8(&key[..i]).unwrap());
            if !s.is_empty() {
                let n = s.len().min(MAX_CLASS_LEN - 1);
                let mut j = 0;
                while j < n { cfg.floating_classes[count][j] = s[j]; j += 1; }
                cfg.floating_classes[count][n] = 0;
                count += 1;
            } else { break; }
            idx += 1;
            if count >= MAX_FLOAT_CLASSES { break; }
        }
        if count > 0 { cfg.floating_class_count = count; }
    }

    /* Commands */
    {
        let term = config_get_string(root_idx, "window_manager.terminal_command");
        if !term.is_empty() {
            let n = term.len().min(MAX_CMD_LEN - 1);
            let mut j = 0;
            while j < n { cfg.terminal_cmd[j] = term[j]; j += 1; }
            cfg.terminal_cmd[n] = 0;
        }
        let launcher = config_get_string(root_idx, "window_manager.launcher_command");
        if !launcher.is_empty() {
            let n = launcher.len().min(MAX_CMD_LEN - 1);
            let mut j = 0;
            while j < n { cfg.launcher_cmd[j] = launcher[j]; j += 1; }
            cfg.launcher_cmd[n] = 0;
        }
    }

    /* Theme accent color */
    {
        let accent = config_get_string(root_idx, "theme.accent");
        if !accent.is_empty() {
            if let Some(c) = parse_hex_color(accent) { cfg.accent = c; }
        }
        let glass_top = config_get_string(root_idx, "theme.glass_top");
        if !glass_top.is_empty() {
            if let Some(c) = parse_hex_color(glass_top) { cfg.glass_top = c; }
        }
        let glass_bottom = config_get_string(root_idx, "theme.glass_bottom");
        if !glass_bottom.is_empty() {
            if let Some(c) = parse_hex_color(glass_bottom) { cfg.glass_bottom = c; }
        }
    }

    CONFIG_LOADED.store(true, Ordering::Relaxed);

    unsafe {
        kprintf(
            b"HYPERDE CONFIG: loaded, workspaces=%u blur=%d shadow=%d anim=%ums\0".as_ptr() as *const c_char,
            cfg.workspace_count as u32,
            cfg.blur_enabled as u32,
            cfg.shadow_enabled as u32,
            cfg.animation_duration_ms,
        );
    }
}

/// Load config from a Lua file via mlua (with require() support)
pub fn config_parse_file(path: &str) {
    let root_idx = execute_with_requires(path);
    if root_idx == 0 {
        unsafe { kprintf(b"HYPERDE CONFIG: failed to load config file\0".as_ptr() as *const c_char, 0, 0, 0, 0); }
        return;
    }
    config_parse_lua_from_root(root_idx);
}

fn config_parse_lua_from_root(root_idx: usize) {
    let cfg = unsafe { &mut CONFIG };
    cfg.border_width = config_get_number(root_idx, "window_manager.border_width", 2.0) as u32;
    cfg.focus_follow_mouse = config_get_bool(root_idx, "window_manager.focus_follow_mouse", true);
    cfg.panel_height = config_get_number(root_idx, "compositor.panel_height", 28.0) as u32;
    cfg.animation_duration_ms = config_get_number(root_idx, "animations.duration", 200.0) as u32;
    cfg.blur_enabled = config_get_bool(root_idx, "decorations.blur", true);
    cfg.shadow_enabled = config_get_bool(root_idx, "decorations.shadow", true);
    cfg.blur_radius = config_get_number(root_idx, "decorations.blur_radius", 8.0) as u32;
    cfg.shadow_blur = config_get_number(root_idx, "decorations.shadow_blur", 4.0) as u32;
    CONFIG_LOADED.store(true, Ordering::Relaxed);
}

fn parse_hex_color(s: &[u8]) -> Option<u32> {
    if s.len() < 7 || s[0] != b'#' { return None; }
    let mut val: u32 = 0;
    let mut i = 1;
    while i < s.len() && i < 9 {
        let h = match s[i] {
            b'0'..=b'9' => s[i] - b'0',
            b'a'..=b'f' => s[i] - b'a' + 10,
            b'A'..=b'F' => s[i] - b'A' + 10,
            _ => return None,
        };
        val = (val << 4) | h as u32;
        i += 1;
    }
    if i == 7 { val = (val << 8) | 0xFF; }
    Some(val)
}

/* ───────────────────────── FFI export ───────────────────────── */

#[no_mangle]
pub unsafe extern "C" fn hyperde_config_load(data: *const u8, len: usize) {
    if data.is_null() || len == 0 { return; }
    let slice = core::slice::from_raw_parts(data, len);
    config_parse_lua(slice);
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_config_workspace_count() -> u32 {
    config().workspace_count as u32
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_config_workspace_name(idx: u32, out: *mut u8, max_len: u32) {
    let cfg = config();
    let i = idx as usize;
    if i >= cfg.workspace_count { return; }
    let name = &cfg.workspace_names[i];
    let mut j = 0;
    while j < name.len() && j < max_len as usize && name[j] != 0 {
        *out.add(j) = name[j]; j += 1;
    }
    if j < max_len as usize { *out.add(j) = 0; }
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_config_accent() -> u32 { config().accent }
#[no_mangle]
pub unsafe extern "C" fn hyperde_config_border_width() -> u32 { config().border_width }
#[no_mangle]
pub unsafe extern "C" fn hyperde_config_panel_height() -> u32 { config().panel_height }
#[no_mangle]
pub unsafe extern "C" fn hyperde_config_blur_enabled() -> bool { config().blur_enabled }
#[no_mangle]
pub unsafe extern "C" fn hyperde_config_shadow_enabled() -> bool { config().shadow_enabled }
#[no_mangle]
pub unsafe extern "C" fn hyperde_config_animation_duration() -> u32 { config().animation_duration_ms }

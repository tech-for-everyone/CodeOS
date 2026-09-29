/* ───────────────────────── HyperDE Keybinding System ─────────────────────────
 * Ported from upstream HyperDE's keybinding dispatch.
 *
 * Supports Penrose-style key combinations:
 *   M-Return  → terminal
 *   M-d       → launcher
 *   M-1..M-9  → workspace switch
 *   M-shift-1..M-9 → move window to workspace
 *   M-q       → close window
 *   M-f       → toggle floating
 *   M-j/M-k   → focus next/prev
 *   M-h/M-l   → resize
 *
 * Key events come from the Qt panels thread via FFI.
 */

use core::ffi::{c_char, c_int};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
}

/* ───────────────────────── Key state ───────────────────────── */

/* Modifier bits */
const MOD_CTRL: u32 = 1 << 0;
const MOD_ALT: u32 = 1 << 1;
const MOD_SHIFT: u32 = 1 << 2;
const MOD_SUPER: u32 = 1 << 3; /* Super/Meta key */

/* In CodeOS, "M-" (Mod4) maps to the Super key */
const MOD_PRIMARY: u32 = MOD_SUPER;

static MOD_HELD: AtomicU32 = AtomicU32::new(0);
static LAUNCHER_OPEN: AtomicBool = AtomicBool::new(false);

/* ───────────────────────── Actions ───────────────────────── */

#[repr(C)]
#[derive(Clone, Copy, PartialEq)]
pub enum HdeAction {
    None,
    SpawnTerminal,
    SpawnLauncher,
    CloseWindow,
    ToggleFloating,
    FocusNext,
    FocusPrev,
    WorkspaceSwitch(u32),
    WorkspaceMove(u32),
    ResizeLeft,
    ResizeRight,
    ResizeUp,
    ResizeDown,
    ToggleFullscreen,
}

impl HdeAction {
    pub fn to_int(self) -> c_int {
        match self {
            HdeAction::None => 0,
            HdeAction::SpawnTerminal => 3,
            HdeAction::SpawnLauncher => 4,
            HdeAction::CloseWindow => 5,
            HdeAction::ToggleFloating => 6,
            HdeAction::FocusNext => 7,
            HdeAction::FocusPrev => 8,
            HdeAction::WorkspaceSwitch(n) => 9 + n as c_int,
            HdeAction::WorkspaceMove(n) => 18 + n as c_int,
            HdeAction::ResizeLeft => 27,
            HdeAction::ResizeRight => 28,
            HdeAction::ResizeUp => 29,
            HdeAction::ResizeDown => 30,
            HdeAction::ToggleFullscreen => 31,
        }
    }
}

/* ───────────────────────── Keybinding table ───────────────────────── */

struct KeyBinding {
    scancode: u32,     /* PS/2 set 1 scancode or 0 for any */
    key: u32,          /* unicode key (lowercase) */
    modifiers: u32,    /* bitmask of MOD_* */
    action: HdeAction,
}

/* Default keybindings (matching upstream HyperDE) */
const BINDINGS: &[KeyBinding] = &[
    /* M-Return → terminal */
    KeyBinding { scancode: 0x1C, key: b'\r' as u32, modifiers: MOD_PRIMARY, action: HdeAction::SpawnTerminal },
    /* M-d → launcher */
    KeyBinding { scancode: 0x20, key: b'd' as u32, modifiers: MOD_PRIMARY, action: HdeAction::SpawnLauncher },
    /* M-q → close window */
    KeyBinding { scancode: 0x10, key: b'q' as u32, modifiers: MOD_PRIMARY, action: HdeAction::CloseWindow },
    /* M-f → toggle floating */
    KeyBinding { scancode: 0x21, key: b'f' as u32, modifiers: MOD_PRIMARY, action: HdeAction::ToggleFloating },
    /* M-j → focus next */
    KeyBinding { scancode: 0x24, key: b'j' as u32, modifiers: MOD_PRIMARY, action: HdeAction::FocusNext },
    /* M-k → focus prev */
    KeyBinding { scancode: 0x25, key: b'k' as u32, modifiers: MOD_PRIMARY, action: HdeAction::FocusPrev },
    /* M-h → resize left */
    KeyBinding { scancode: 0x23, key: b'h' as u32, modifiers: MOD_PRIMARY, action: HdeAction::ResizeLeft },
    /* M-l → resize right */
    KeyBinding { scancode: 0x26, key: b'l' as u32, modifiers: MOD_PRIMARY, action: HdeAction::ResizeRight },
    /* M-1..M-9 → workspace switch */
    KeyBinding { scancode: 0x02, key: b'1' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(0) },
    KeyBinding { scancode: 0x03, key: b'2' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(1) },
    KeyBinding { scancode: 0x04, key: b'3' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(2) },
    KeyBinding { scancode: 0x05, key: b'4' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(3) },
    KeyBinding { scancode: 0x06, key: b'5' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(4) },
    KeyBinding { scancode: 0x07, key: b'6' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(5) },
    KeyBinding { scancode: 0x08, key: b'7' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(6) },
    KeyBinding { scancode: 0x09, key: b'8' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(7) },
    KeyBinding { scancode: 0x0A, key: b'9' as u32, modifiers: MOD_PRIMARY, action: HdeAction::WorkspaceSwitch(8) },
    /* M-S-1..M-S-9 → move window to workspace */
    KeyBinding { scancode: 0x02, key: b'1' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(0) },
    KeyBinding { scancode: 0x03, key: b'2' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(1) },
    KeyBinding { scancode: 0x04, key: b'3' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(2) },
    KeyBinding { scancode: 0x05, key: b'4' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(3) },
    KeyBinding { scancode: 0x06, key: b'5' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(4) },
    KeyBinding { scancode: 0x07, key: b'6' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(5) },
    KeyBinding { scancode: 0x08, key: b'7' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(6) },
    KeyBinding { scancode: 0x09, key: b'8' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(7) },
    KeyBinding { scancode: 0x0A, key: b'9' as u32, modifiers: MOD_PRIMARY | MOD_SHIFT, action: HdeAction::WorkspaceMove(8) },
    /* M-F11 → toggle fullscreen */
    KeyBinding { scancode: 0x57, key: 0, modifiers: MOD_PRIMARY, action: HdeAction::ToggleFullscreen },
];

/* ───────────────────────── Modifier tracking ───────────────────────── */

/// Map PS/2 set 1 scancode to modifier bit
fn scancode_to_mod(sc: u32) -> u32 {
    match sc {
        0x1D => MOD_CTRL,       /* Left Ctrl */
        0x2A | 0x36 => MOD_SHIFT, /* Left/Right Shift */
        0x38 => MOD_ALT,        /* Left Alt */
        0x5B | 0x5C => MOD_SUPER, /* Left/Right Super */
        _ => 0,
    }
}

/// Map PS/2 set 1 scancode to lowercase ASCII (unshifted)
fn scancode_to_key(sc: u32) -> u32 {
    match sc {
        0x02..=0x0B => b'1' as u32 + (sc - 0x02), /* 1-9, 0 */
        0x10 => b'q' as u32, 0x11 => b'w' as u32, 0x12 => b'e' as u32, 0x13 => b'r' as u32,
        0x14 => b't' as u32, 0x15 => b'y' as u32, 0x16 => b'u' as u32, 0x17 => b'i' as u32,
        0x18 => b'o' as u32, 0x19 => b'p' as u32, 0x1E => b'a' as u32, 0x1F => b's' as u32,
        0x20 => b'd' as u32, 0x21 => b'f' as u32, 0x22 => b'g' as u32, 0x23 => b'h' as u32,
        0x24 => b'j' as u32, 0x25 => b'k' as u32, 0x26 => b'l' as u32, 0x2C => b'z' as u32,
        0x2D => b'x' as u32, 0x2E => b'c' as u32, 0x2F => b'v' as u32, 0x30 => b'b' as u32,
        0x31 => b'n' as u32, 0x32 => b'm' as u32,
        0x39 => b' ' as u32,          /* Space */
        0x1C => b'\r' as u32,         /* Enter */
        0x0E => b'\x08' as u32,       /* Backspace */
        0x57 => 0,                     /* F11 (no ascii) */
        _ => 0,
    }
}

/* ───────────────────────── Public API ───────────────────────── */

/// Process a key press event. Returns the action to perform.
/// Called from the Qt panels thread on every keydown.
#[no_mangle]
pub unsafe extern "C" fn hyperde_keypress(scancode: u32, _keycode: u32) -> c_int {
    let mods_before = MOD_HELD.load(Ordering::Relaxed);

    /* update modifier state */
    let mod_bit = scancode_to_mod(scancode);
    if mod_bit != 0 {
        MOD_HELD.store(mods_before | mod_bit, Ordering::Relaxed);
        return 0;
    }

    let current_mods = MOD_HELD.load(Ordering::Relaxed);
    let key = scancode_to_key(scancode);

    /* check bindings */
    for b in BINDINGS {
        if (b.scancode == scancode || (b.key != 0 && b.key == key)) && b.modifiers == current_mods {
            unsafe {
                kprintf(
                    b"HYPERDE KEYBIND: matched action %d (mods=0x%x key=0x%x)\0".as_ptr() as *const core::ffi::c_char,
                    b.action.to_int(),
                    current_mods,
                    key,
                );
            }
            return b.action.to_int();
        }
    }

    0
}

/// Process a key release event. Updates modifier state.
#[no_mangle]
pub unsafe extern "C" fn hyperde_keyrelease(scancode: u32) {
    let mod_bit = scancode_to_mod(scancode);
    if mod_bit != 0 {
        MOD_HELD.fetch_and(!mod_bit, Ordering::Relaxed);
    }
}

/// Check if the launcher overlay is open
pub fn is_open() -> bool {
    LAUNCHER_OPEN.load(Ordering::Relaxed)
}

/// Toggle the launcher overlay
pub fn toggle() {
    let was = LAUNCHER_OPEN.load(Ordering::Relaxed);
    LAUNCHER_OPEN.store(!was, Ordering::Relaxed);
}

/// Close the launcher
pub fn close() {
    LAUNCHER_OPEN.store(false, Ordering::Relaxed);
}

/// Public scancode→key mapping (used by launcher for text input)
pub fn scancode_to_key_pub(sc: u32) -> u32 {
    scancode_to_key(sc)
}

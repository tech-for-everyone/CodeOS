/* ───────────────────────── HyperDE App Launcher ─────────────────────────
 * dmenu-style overlay launcher ported from upstream HyperDE.
 *
 * When opened (via M-d or clicking the launcher button in the bar),
 * a translucent overlay appears with a search input and a list of
 * registered applications. Typing filters the list; Enter launches
 * the selected app.
 *
 * Apps are registered at boot via hyperde_launcher_register().
 */

use core::ffi::{c_char, c_int};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use crate::{glass_px, round_rect, draw_text};
use crate::pal;

extern "C" {
    fn kprintf(fmt: *const c_char, ...);
    fn fb_getwidth() -> u32;
    fn fb_getheight() -> u32;
    fn fb_get_pitch() -> c_int;
    fn fb_get_active_buffer() -> *mut u32;
}

/* ───────────────────────── App registry ───────────────────────── */

const MAX_APPS: usize = 64;
const MAX_NAME: usize = 32;
const MAX_CMD: usize = 64;

#[repr(C)]
#[derive(Copy, Clone)]
struct AppEntry {
    name: [u8; MAX_NAME],
    cmd: [u8; MAX_CMD],
    icon_char: u8,  /* single ASCII char for icon */
}

static mut APPS: [AppEntry; MAX_APPS] = [AppEntry {
    name: [0; MAX_NAME],
    cmd: [0; MAX_CMD],
    icon_char: b'>',
}; MAX_APPS];
static mut APP_COUNT: usize = 0;

/* ───────────────────────── Launcher state ───────────────────────── */

static OPEN: AtomicBool = AtomicBool::new(false);
static SEL_IDX: AtomicU32 = AtomicU32::new(0);
static FILTER_COUNT: AtomicU32 = AtomicU32::new(0);
static INPUT_LEN: AtomicU32 = AtomicU32::new(0);
static mut INPUT_BUF: [u8; 48] = [0u8; 48];
/* indices of apps matching the current filter */
static mut FILTER_IDX: [u32; MAX_APPS] = [0; MAX_APPS];
/* set to the app index when the user selects one; C side polls and clears */
static LAUNCH_IDX: AtomicU32 = AtomicU32::new(u32::MAX);

/* ───────────────────────── Colors ───────────────────────── */

const OVERLAY_ALPHA: u32 = 0xB0;
const ITEM_H: i64 = 32;
const MAX_VISIBLE: usize = 15;

/* ───────────────────────── Register an app ───────────────────────── */

#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_register(
    name: *const c_char,
    cmd: *const c_char,
    icon: u8,
) {
    if APP_COUNT >= MAX_APPS {
        return;
    }
    let app = &mut APPS[APP_COUNT];
    /* copy name */
    let mut i = 0;
    while i < MAX_NAME - 1 && *name.add(i) != 0 {
        app.name[i] = *name.add(i) as u8;
        i += 1;
    }
    app.name[i] = 0;
    /* copy cmd */
    let mut i = 0;
    while i < MAX_CMD - 1 && *cmd.add(i) != 0 {
        app.cmd[i] = *cmd.add(i) as u8;
        i += 1;
    }
    app.cmd[i] = 0;
    app.icon_char = icon;
    APP_COUNT += 1;
}

/* ───────────────────────── Filter logic ───────────────────────── */

unsafe fn apply_filter() {
    let input = &INPUT_BUF[..INPUT_LEN.load(Ordering::Relaxed) as usize];
    let mut count = 0u32;
    for i in 0..APP_COUNT {
        if input.is_empty() {
            /* show all when input is empty */
            FILTER_IDX[count as usize] = i as u32;
            count += 1;
        } else {
            /* case-insensitive substring match */
            let name = &APPS[i].name;
            let mut matched = false;
            'outer: for start in 0..MAX_NAME {
                if name[start] == 0 {
                    break;
                }
                let mut ok = true;
                for j in 0..input.len() {
                    let nc = if start + j < MAX_NAME { name[start + j] } else { 0 };
                    if nc == 0 {
                        ok = false;
                        break;
                    }
                    let ic = input[j];
                    let nc_lo = if nc >= b'A' && nc <= b'Z' { nc + 32 } else { nc };
                    let ic_lo = if ic >= b'A' && ic <= b'Z' { ic + 32 } else { ic };
                    if nc_lo != ic_lo {
                        ok = false;
                        break;
                    }
                }
                if ok {
                    matched = true;
                    break 'outer;
                }
            }
            if matched {
                FILTER_IDX[count as usize] = i as u32;
                count += 1;
            }
        }
    }
    FILTER_COUNT.store(count, Ordering::Relaxed);
    if SEL_IDX.load(Ordering::Relaxed) >= count {
        SEL_IDX.store(0, Ordering::Relaxed);
    }
}

/* ───────────────────────── Render ───────────────────────── */

unsafe fn render_launcher(buf: *mut u32, stride: u32, w: u32, h: u32) {
    let accent = pal().accent;
    let text = pal().text;
    let sub = pal().sub;
    let glass_top = pal().glass_top;
    let fw = w as i64;
    let fh = h as i64;

    /* full-screen dim overlay */
    for y in 0..fh {
        for x in 0..fw {
            glass_px(buf, stride, w, h, x, y, 0x00000000, OVERLAY_ALPHA);
        }
    }

    /* centered search box */
    let box_w = 480i64;
    let box_h = ITEM_H + 8;
    let box_x = (fw - box_w) / 2;
    let box_y = fh / 3 - box_h;

    /* search input background */
    round_rect(buf, stride, w, h, box_x, box_y, box_x + box_w, box_y + box_h, 12, glass_top, 0xE8);
    /* border accent */
    for x in box_x..box_x + box_w {
        glass_px(buf, stride, w, h, x, box_y, accent, 0x88);
        glass_px(buf, stride, w, h, x, box_y + box_h - 1, accent, 0x44);
    }
    for y in box_y..box_y + box_h {
        glass_px(buf, stride, w, h, box_x, y, accent, 0x44);
        glass_px(buf, stride, w, h, box_x + box_w - 1, y, accent, 0x44);
    }

    /* search text */
    let input = &INPUT_BUF[..INPUT_LEN.load(Ordering::Relaxed) as usize];
    draw_text(buf, stride, w, h, box_x + 16, box_y + (box_h - 8) / 2, input, text, 1, 0xFF);
    /* cursor blink (draw a thin line) */
    let cursor_x = box_x + 16 + input.len() as i64 * 10;
    if cursor_x < box_x + box_w - 16 {
        for y in box_y + 6..box_y + box_h - 6 {
            glass_px(buf, stride, w, h, cursor_x, y, text, 0xCC);
        }
    }

    /* app list */
    let fcount = FILTER_COUNT.load(Ordering::Relaxed) as usize;
    let sel = SEL_IDX.load(Ordering::Relaxed) as usize;
    let list_y = box_y + box_h + 8;
    let visible = fcount.min(MAX_VISIBLE);

    for i in 0..visible {
        let iy = list_y + i as i64 * ITEM_H;
        let is_selected = i == sel;

        /* item background with selection indicator */
        if is_selected {
            round_rect(buf, stride, w, h, box_x, iy, box_x + box_w, iy + ITEM_H - 2, 8, accent, 0x30);
            /* left accent bar for selected item */
            for y in iy + 4..iy + ITEM_H - 6 {
                glass_px(buf, stride, w, h, box_x + 2, y, accent, 0xCC);
            }
        }

        /* icon */
        let icon_ch = APPS[FILTER_IDX[i] as usize].icon_char;
        let icon_buf = [icon_ch];
        draw_text(buf, stride, w, h, box_x + 12, iy + (ITEM_H - 8) / 2, &icon_buf, if is_selected { accent } else { sub }, 1, 0xFF);

        /* app name (highlight matching characters in accent) */
        let name = &APPS[FILTER_IDX[i] as usize].name;
        let mut nlen = 0;
        while nlen < MAX_NAME && name[nlen] != 0 {
            nlen += 1;
        }
        let inp = &INPUT_BUF[..INPUT_LEN.load(Ordering::Relaxed) as usize];
        if inp.len() > 0 {
            /* render with match highlighting */
            let mut nx = box_x + 32;
            let ny = iy + (ITEM_H - 8) / 2;
            for ci in 0..nlen {
                let ch = name[ci];
                let matches = inp.iter().any(|&ic| ic == ch || ic.to_ascii_lowercase() == ch.to_ascii_lowercase());
                let col = if matches { accent } else if is_selected { text } else { sub };
                let ch_slice = &name[ci..ci + 1];
                draw_text(buf, stride, w, h, nx, ny, ch_slice, col, 1, 0xFF);
                nx += 10;
            }
        } else {
            draw_text(buf, stride, w, h, box_x + 32, iy + (ITEM_H - 8) / 2, &name[..nlen], if is_selected { text } else { sub }, 1, 0xFF);
        }

        /* hint on right side: the command */
        let cmd = &APPS[FILTER_IDX[i] as usize].cmd;
        let mut clen = 0;
        while clen < MAX_CMD && cmd[clen] != 0 {
            clen += 1;
        }
        if clen > 12 {
            clen = 12;
        }
        draw_text(buf, stride, w, h, box_x + box_w - 20 - clen as i64 * 10, iy + (ITEM_H - 8) / 2, &cmd[..clen], sub, 1, 0x80);
    }

    /* hint text at bottom */
    if fcount == 0 {
        draw_text(buf, stride, w, h, box_x + 16, list_y + 8, b"no matching apps", sub, 1, 0x80);
    } else {
        draw_text(buf, stride, w, h, box_x + 16, list_y + 8, b"enter: launch  esc: close", sub, 1, 0x50);
    }
}

/* ───────────────────────── Public API ───────────────────────── */

#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_is_open() -> c_int {
    if OPEN.load(Ordering::Relaxed) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_open() {
    if OPEN.load(Ordering::Relaxed) {
        return;
    }
    OPEN.store(true, Ordering::Relaxed);
    INPUT_LEN.store(0, Ordering::Relaxed);
    INPUT_BUF[0] = 0;
    SEL_IDX.store(0, Ordering::Relaxed);
    apply_filter();
    kprintf(b"HYPERDE LAUNCHER: opened (%u apps)\0".as_ptr() as *const c_char, APP_COUNT as u32);
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_close() {
    OPEN.store(false, Ordering::Relaxed);
    kprintf(b"HYPERDE LAUNCHER: closed\0".as_ptr() as *const c_char);
}

#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_toggle() {
    if OPEN.load(Ordering::Relaxed) {
        hyperde_launcher_close();
    } else {
        hyperde_launcher_open();
    }
}

/// Handle a keypress while the launcher is open. Returns 1 if the
/// launcher consumed the event, 0 if it should fall through.
#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_key(scancode: u32) -> c_int {
    if !OPEN.load(Ordering::Relaxed) {
        return 0;
    }

    let fcount = FILTER_COUNT.load(Ordering::Relaxed);

    match scancode {
        0x01 => { /* Escape → close */
            hyperde_launcher_close();
            return 1;
        }
        0x48 => { /* Up arrow */
            let sel = SEL_IDX.load(Ordering::Relaxed);
            if sel > 0 {
                SEL_IDX.store(sel - 1, Ordering::Relaxed);
            }
            return 1;
        }
        0x50 => { /* Down arrow */
            let sel = SEL_IDX.load(Ordering::Relaxed);
            if sel + 1 < fcount {
                SEL_IDX.store(sel + 1, Ordering::Relaxed);
            }
            return 1;
        }
        0x1C => { /* Enter → launch selected */
            let sel = SEL_IDX.load(Ordering::Relaxed) as usize;
            if sel < fcount as usize {
                let idx = FILTER_IDX[sel] as usize;
                let cmd = &APPS[idx].cmd;
                let mut clen = 0;
                while clen < MAX_CMD && cmd[clen] != 0 {
                    clen += 1;
                }
                kprintf(
                    b"HYPERDE LAUNCHER: launching '%s'\0".as_ptr() as *const c_char,
                    cmd.as_ptr() as *const c_char,
                );
                /* store the app index for the C side to poll */
                LAUNCH_IDX.store(FILTER_IDX[sel], Ordering::Relaxed);
                hyperde_launcher_close();
            }
            return 1;
        }
        0x0E => { /* Backspace */
            let len = INPUT_LEN.load(Ordering::Relaxed) as usize;
            if len > 0 {
                INPUT_BUF[len - 1] = 0;
                INPUT_LEN.store((len - 1) as u32, Ordering::Relaxed);
                apply_filter();
            }
            return 1;
        }
        _ => {}
    }

    /* character input: map scancode to key and append */
    let key = crate::keybind::scancode_to_key_pub(scancode);
    if key >= 0x20 && key < 0x7F {
        let len = INPUT_LEN.load(Ordering::Relaxed) as usize;
        if len < 47 {
            INPUT_BUF[len] = key as u8;
            INPUT_LEN.store((len + 1) as u32, Ordering::Relaxed);
            apply_filter();
        }
        return 1;
    }

    0
}

/// Render the launcher overlay if open. Called from the pump.
/// Returns true if the launcher was rendered (caller should skip
/// normal bar rendering for this frame).
#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_render() -> c_int {
    if !OPEN.load(Ordering::Relaxed) {
        return 0;
    }
    let buf = fb_get_active_buffer();
    if buf.is_null() {
        return 0;
    }
    let w = fb_getwidth();
    let h = fb_getheight();
    let pitch = fb_get_pitch();
    let stride = if pitch > 0 { (pitch as u32) / 4 } else { w };
    render_launcher(buf, stride, w, h);
    1
}

/// Poll for a launch request. Returns the index of the app to launch,
/// or u32::MAX if none. The C side should call this after pump() and
/// use the returned index to look up and launch the app.
#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_poll_launch() -> u32 {
    LAUNCH_IDX.swap(u32::MAX, Ordering::Relaxed)
}

/// Get the command string for a registered app by index.
/// Returns 1 if found, 0 if not.
#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_get_cmd(idx: u32, out: *mut u8, max_len: u32) -> c_int {
    if idx as usize >= APP_COUNT {
        return 0;
    }
    let cmd = &APPS[idx as usize].cmd;
    let mut i = 0;
    while i < MAX_CMD && cmd[i] != 0 && i < max_len as usize {
        *out.add(i) = cmd[i];
        i += 1;
    }
    if i < max_len as usize {
        *out.add(i) = 0;
    }
    1
}

/// Get the name string for a registered app by index.
#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_get_name(idx: u32, out: *mut u8, max_len: u32) -> c_int {
    if idx as usize >= APP_COUNT {
        return 0;
    }
    let name = &APPS[idx as usize].name;
    let mut i = 0;
    while i < MAX_NAME && name[i] != 0 && i < max_len as usize {
        *out.add(i) = name[i];
        i += 1;
    }
    if i < max_len as usize {
        *out.add(i) = 0;
    }
    1
}

/// Get the total number of registered apps.
#[no_mangle]
pub unsafe extern "C" fn hyperde_launcher_count() -> u32 {
    APP_COUNT as u32
}

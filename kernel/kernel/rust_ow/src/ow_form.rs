//! ow_form.rs — OpenWeb's form API and per-control edit cache, in Rust.
//!
//! This is the C-to-Rust continuation of the OpenWeb rewrite. The HTML
//! renderer already lives in Rust (`ow_render.rs`); what used to be the last
//! non-trivial C in `qt6/panels/ow_html.c` was this file: the exported form
//! API the Qt frontend calls (`ow_field_set_value/toggle/at`,
//! `ow_form_build_query`) plus the persistent per-control edit cache that the
//! renderer re-applies on every re-render.
//!
//! Behaviour is a faithful port of the old C, including the exact quirks:
//!   - the edit cache is keyed by (enclosing form action, field name, type),
//!     so a user's text survives a re-render and is dropped the moment the
//!     page structure around it changes;
//!   - `ow_form_build_query` skips unchecked checkboxes/radios, buttons and
//!     nameless controls, uses `+` for spaces in values but `%20` in names,
//!     and substitutes `on` for a checked control with an empty value;
//!   - a `select` whose requested value matches no option falls back to the
//!     last option.
//!
//! The C-owned output tables (`ow_forms`, `ow_form_fields`, `ow_form_cnt`,
//! `ow_field_cnt`) stay in `ow_html.c` — they are the shared contract with the
//! Qt frontend — and are reached here through the same `extern "C"` statics
//! the renderer uses.
//!
//! Entry points (C ABI, matching `qt6/panels/ow_html.h`):
//!   ow_field_set_value, ow_field_toggle, ow_field_at, ow_form_build_query,
//!   ow_fv_restore, ow_fv_store

#![allow(static_mut_refs)]

use core::ffi::{c_char, c_int};

use crate::ow_render::{
    ow_field_cnt, ow_form_cnt, ow_form_fields, ow_forms, OwForm, OwFormField, OW_FT_BUTTON,
    OW_FT_CHECKBOX, OW_FT_RADIO, OW_FT_SELECT, OW_FT_SUBMIT, OW_MAX_FIELDS, OW_URL_MAX,
};

/* ── persistent per-control edit cache ──
 * Keyed by the enclosing form's action + the field's name + type. The renderer
 * rebuilds the tables on every paint, so user-typed values live here and are
 * re-applied to the matching new field on the next render. */
static mut S_FV_NAME: [[u8; OW_URL_MAX]; OW_MAX_FIELDS] = [[0; OW_URL_MAX]; OW_MAX_FIELDS];
static mut S_FV_TYPE: [u8; OW_MAX_FIELDS] = [0; OW_MAX_FIELDS];
static mut S_FV_ACTION: [[u8; OW_URL_MAX]; OW_MAX_FIELDS] = [[0; OW_URL_MAX]; OW_MAX_FIELDS];
static mut S_FV_VALUE: [[u8; OW_URL_MAX]; OW_MAX_FIELDS] = [[0; OW_URL_MAX]; OW_MAX_FIELDS];
static mut S_FV_CHECKED: [u8; OW_MAX_FIELDS] = [0; OW_MAX_FIELDS];
static mut S_FV_SEL: [u8; OW_MAX_FIELDS] = [0; OW_MAX_FIELDS];
static mut S_FV_EDITED: [u8; OW_MAX_FIELDS] = [0; OW_MAX_FIELDS];

/* ── byte helpers (no libc; this crate is no_std) ── */

/// Copy a NUL-terminated `src` slice into `dst`, NUL-terminated, bounded by
/// `dst.len()` — the exact contract of the old `ow_strncpy_n()`.
fn scopy(dst: &mut [u8], src: &[u8]) {
    let mut i = 0;
    while i < src.len() && src[i] != 0 && i < dst.len() - 1 {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = 0;
}

/// Byte-wise equality treating a slice's end as its NUL terminator, like
/// `strcmp(a, b) == 0` on two fixed NUL-terminated buffers.
fn seq(a: &[u8], b: &[u8]) -> bool {
    let mut i = 0;
    loop {
        let ca = if i < a.len() { a[i] } else { 0 };
        let cb = if i < b.len() { b[i] } else { 0 };
        if ca != cb {
            return false;
        }
        if ca == 0 {
            return true;
        }
        i += 1;
    }
}

/// `strcmp(a, C-string p) == 0`, reading `p` only up to its NUL.
fn ptr_eq(a: &[u8], p: *const u8) -> bool {
    if p.is_null() {
        return a[0] == 0;
    }
    let mut i = 0;
    loop {
        let ca = if i < a.len() { a[i] } else { 0 };
        let cb = unsafe { *p.add(i) };
        if ca != cb {
            return false;
        }
        if ca == 0 {
            return true;
        }
        i += 1;
    }
}

/// Find the form a field belongs to (or -1).
unsafe fn field_form_idx(fi: c_int) -> c_int {
    let cnt = ow_form_cnt;
    let mut k: c_int = 0;
    while k < cnt {
        let f = &ow_forms[k as usize];
        let start = f.field_start as c_int;
        let end = start + f.field_count as c_int;
        if fi >= start && fi < end {
            return k;
        }
        k += 1;
    }
    -1
}

/// Copy the enclosing form's action (for the cache key) into `out`, or "".
unsafe fn field_form_action_into(out: &mut [u8], fi: c_int) {
    let k = field_form_idx(fi);
    if k >= 0 {
        let a = &ow_forms[k as usize].action;
        scopy(out, a);
    } else {
        out[0] = 0;
    }
}

/* ── edit-cache bridge used by the Rust renderer around each control ── */

/// Apply a cached user edit to a freshly parsed field, if the field identity
/// (form action, name, type) matches the cache slot.
#[no_mangle]
pub unsafe extern "C" fn ow_fv_restore(f: *mut OwFormField, fi: c_int) {
    if f.is_null() || fi < 0 || fi >= OW_MAX_FIELDS as c_int {
        return;
    }
    let f = &mut *f;
    let idx = fi as usize;

    if S_FV_EDITED[idx] == 0 {
        S_FV_NAME[idx][0] = 0;
        return;
    }
    let mut act = [0u8; OW_URL_MAX];
    field_form_action_into(&mut act, fi);
    if !seq(&S_FV_NAME[idx], &f.name) || S_FV_TYPE[idx] != f.typ || !seq(&S_FV_ACTION[idx], &act) {
        S_FV_EDITED[idx] = 0;
        S_FV_NAME[idx][0] = 0;
        return;
    }

    if f.typ == OW_FT_CHECKBOX || f.typ == OW_FT_RADIO {
        f.checked = if S_FV_CHECKED[idx] != 0 { 1 } else { 0 };
        if f.typ == OW_FT_RADIO && f.checked != 0 {
            let v = S_FV_VALUE[idx];
            scopy(&mut f.value, &v);
        }
    } else if f.typ == OW_FT_SELECT {
        f.value[0] = 0;
        if (S_FV_SEL[idx] as c_int) < (f.opt_cnt as c_int) {
            f.opt_sel = S_FV_SEL[idx];
            let opt = f.opts[f.opt_sel as usize];
            scopy(&mut f.value, &opt);
        }
    } else {
        let v = S_FV_VALUE[idx];
        scopy(&mut f.value, &v);
    }
}

/// Persist the field's current identity + values into the cache slot.
#[no_mangle]
pub unsafe extern "C" fn ow_fv_store(f: *const OwFormField, fi: c_int) {
    if f.is_null() || fi < 0 || fi >= OW_MAX_FIELDS as c_int {
        return;
    }
    let f = &*f;
    let idx = fi as usize;

    let name = f.name;
    scopy(&mut S_FV_NAME[idx], &name);
    S_FV_TYPE[idx] = f.typ;
    let mut act = [0u8; OW_URL_MAX];
    field_form_action_into(&mut act, fi);
    scopy(&mut S_FV_ACTION[idx], &act);

    if f.typ != OW_FT_CHECKBOX {
        let v = f.value;
        scopy(&mut S_FV_VALUE[idx], &v);
    }
    if S_FV_EDITED[idx] != 0 {
        if f.typ == OW_FT_CHECKBOX {
            S_FV_CHECKED[idx] = if f.checked != 0 { 1 } else { 0 };
        }
    } else {
        S_FV_CHECKED[idx] = if f.checked != 0 { 1 } else { 0 };
    }
    S_FV_SEL[idx] = f.opt_sel;
}

/* ── exported form API ── */

#[no_mangle]
pub unsafe extern "C" fn ow_field_set_value(fi: c_int, v: *const c_char) {
    if fi < 0 || fi >= ow_field_cnt {
        return;
    }
    let idx = fi as usize;

    if ow_form_fields[idx].typ == OW_FT_SELECT {
        let opt_cnt = ow_form_fields[idx].opt_cnt;
        let mut k: u8 = 0;
        while k < opt_cnt {
            let opt = ow_form_fields[idx].opts[k as usize];
            let eq = if v.is_null() {
                opt[0] == 0
            } else {
                ptr_eq(&opt, v as *const u8)
            };
            if eq {
                let f = &mut ow_form_fields[idx];
                f.opt_sel = k;
                scopy(&mut f.value, &opt);
                S_FV_EDITED[idx] = 1;
                S_FV_SEL[idx] = k;
                return;
            }
            k += 1;
        }
        let f = &mut ow_form_fields[idx];
        f.opt_sel = if f.opt_cnt != 0 { f.opt_cnt - 1 } else { 0 };
        return;
    }

    {
        let f = &mut ow_form_fields[idx];
        if v.is_null() {
            f.value[0] = 0;
        } else {
            let n = cstr_len(v as *const u8, OW_URL_MAX - 1);
            let mut i = 0;
            while i < n {
                f.value[i] = *(v as *const u8).add(i);
                i += 1;
            }
            f.value[i] = 0;
        }
        S_FV_EDITED[idx] = 1;
        let val = f.value;
        scopy(&mut S_FV_VALUE[idx], &val);
    }
}

#[no_mangle]
pub unsafe extern "C" fn ow_field_toggle(fi: c_int) {
    if fi < 0 || fi >= ow_field_cnt {
        return;
    }
    let idx = fi as usize;
    let typ = ow_form_fields[idx].typ;
    let form = ow_form_fields[idx].form;
    let name = ow_form_fields[idx].name;

    if typ == OW_FT_CHECKBOX {
        let f = &mut ow_form_fields[idx];
        f.checked = if f.checked != 0 { 0 } else { 1 };
        S_FV_EDITED[idx] = 1;
        S_FV_CHECKED[idx] = f.checked;
    } else if typ == OW_FT_RADIO {
        let mut k: c_int = 0;
        while k < ow_field_cnt {
            let ku = k as usize;
            let rtyp = ow_form_fields[ku].typ;
            let rform = ow_form_fields[ku].form;
            let rname = ow_form_fields[ku].name;
            if rtyp == OW_FT_RADIO && rform == form && seq(&rname, &name) {
                if k == fi {
                    let r = &mut ow_form_fields[ku];
                    r.checked = 1;
                    S_FV_CHECKED[ku] = 1;
                    S_FV_EDITED[ku] = 1;
                    let rv = r.value;
                    scopy(&mut S_FV_VALUE[ku], &rv);
                    S_FV_EDITED[idx] = 1;
                } else {
                    let r = &mut ow_form_fields[ku];
                    r.checked = 0;
                    S_FV_CHECKED[ku] = 0;
                }
            }
            k += 1;
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ow_field_at(line: c_int, col: c_int) -> c_int {
    if line < 0 {
        return -1;
    }
    let mut k: c_int = 0;
    while k < ow_field_cnt {
        let f = &ow_form_fields[k as usize];
        if f.line == line && col >= f.col && col < f.col + f.width {
            return k;
        }
        k += 1;
    }
    -1
}

/* URL-encode one name/value pair into out; returns bytes written. */
unsafe fn qpart(out: *mut u8, out_max: usize, name: &[u8], val: &[u8]) -> usize {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let unres = |b: u8| -> bool {
        b.is_ascii_alphanumeric() || b == b'-' || b == b'_' || b == b'.' || b == b'~'
    };
    let mut n = 0usize;

    let mut i = 0;
    while i < name.len() && name[i] != 0 {
        let c = name[i];
        if n + 3 >= out_max {
            return n;
        }
        if unres(c) {
            *out.add(n) = c;
            n += 1;
        } else {
            *out.add(n) = b'%';
            *out.add(n + 1) = HEX[(c >> 4) as usize];
            *out.add(n + 2) = HEX[(c & 15) as usize];
            n += 3;
        }
        i += 1;
    }
    if n + 1 >= out_max {
        return n;
    }
    *out.add(n) = b'=';
    n += 1;

    let mut i = 0;
    while i < val.len() && val[i] != 0 {
        let c = val[i];
        if n + 3 >= out_max {
            return n;
        }
        if c == b' ' {
            *out.add(n) = b'+';
            n += 1;
        } else if unres(c) {
            *out.add(n) = c;
            n += 1;
        } else {
            *out.add(n) = b'%';
            *out.add(n + 1) = HEX[(c >> 4) as usize];
            *out.add(n + 2) = HEX[(c & 15) as usize];
            n += 3;
        }
        i += 1;
    }
    n
}

/// Build "name=value&..." (URL-encoded, skips unchecked + buttons). Returns
/// the byte length written (excluding the NUL).
#[no_mangle]
pub unsafe extern "C" fn ow_form_build_query(
    f: *const OwForm,
    out: *mut c_char,
    out_max: c_int,
) -> c_int {
    if f.is_null() || out.is_null() || out_max <= 0 {
        return 0;
    }
    let f = &*f;
    let out = out as *mut u8;
    let omax = out_max as usize;
    *out = 0;

    let mut n = 0usize;
    let start = f.field_start as c_int;
    let end = start + f.field_count as c_int;
    let mut k = start;
    while k < end && k < ow_field_cnt {
        let fd = &ow_form_fields[k as usize];
        let typ = fd.typ;
        if typ == OW_FT_SUBMIT || typ == OW_FT_BUTTON {
            k += 1;
            continue;
        }
        if (typ == OW_FT_CHECKBOX || typ == OW_FT_RADIO) && fd.checked == 0 {
            k += 1;
            continue;
        }
        if fd.name[0] == 0 {
            k += 1;
            continue;
        }
        if n != 0 {
            if n + 1 >= omax {
                break;
            }
            *out.add(n) = b'&';
            n += 1;
        }
        let mut qv = fd.value;
        if (typ == OW_FT_CHECKBOX || typ == OW_FT_RADIO) && qv[0] == 0 {
            qv[0] = b'o';
            qv[1] = b'n';
            qv[2] = 0;
        }
        n += qpart(out.add(n), omax - n, &fd.name, &qv);
        k += 1;
    }
    *out.add(n) = 0;
    n as c_int
}

/// Bounded `strlen` of a C string.
fn cstr_len(p: *const u8, max: usize) -> usize {
    let mut n = 0;
    unsafe {
        while n < max && *p.add(n) != 0 {
            n += 1;
        }
    }
    n
}

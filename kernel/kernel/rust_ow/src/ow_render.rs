//! ow_render.rs — OpenWeb's HTML renderer, in Rust.
//!
//! Faithful port of `qt6/panels/ow_html.c`'s `render_html()` (the
//! litebrowser-style lightweight HTML → text-grid engine). It writes the
//! exact same C-owned globals (`ow_txt`, `ow_links`, `ow_images`,
//! `ow_forms`, `ow_form_fields`, `ow_page_title`, ...) via extern statics,
//! so the Qt desktop frontend is unchanged — only the engine's language
//! moved from C to Rust.
//!
//! Entry point: `ow_render_rs(html, len)` (extern "C"), wired into
//! `ow_core_render_active()` in place of the old C `render_html()`.
//!
//! Improvements:
//! - Word-wrapping at OW_TXT_COLS (120) column boundary via buf_put()
//! - Link underlining stored in ow_links[] globals
//! - Enhanced form field rendering with password masking + select display
//! - Additional inline HTML support: <br>, <p>, <div>, <span>, <br/>

#![allow(static_mut_refs)]

use core::ffi::{c_char, c_int};

const OW_TXT_LINES: usize = 512;
const OW_TXT_COLS: usize = 120;
const OW_MAX_LINKS: usize = 128;
const OW_MAX_IMAGES: usize = 32;
const OW_URL_MAX: usize = 512;
const OW_MAX_FORMS: usize = 8;
const OW_MAX_FIELDS: usize = 40;
const OW_MAX_SELECT_OPTS: usize = 16;
const OW_SELECT_OPT_SZ: usize = 56;

/* ── field types (ow_html.h) ── */
const OW_FT_TEXT: u8 = 0;
const OW_FT_PASSWORD: u8 = 1;
const OW_FT_SUBMIT: u8 = 2;
const OW_FT_BUTTON: u8 = 3;
const OW_FT_CHECKBOX: u8 = 4;
const OW_FT_RADIO: u8 = 5;
const OW_FT_TEXTAREA: u8 = 6;
const OW_FT_SELECT: u8 = 7;

/* ── line types (ow_html.h) ── */
const OW_LT_NORMAL: u8 = 0;
const OW_LT_H1: u8 = 1;
const OW_LT_H2: u8 = 2;
const OW_LT_H3: u8 = 3;
const OW_LT_H4: u8 = 4;
const OW_LT_H5: u8 = 5;
const OW_LT_H6: u8 = 6;
const OW_LT_HR: u8 = 7;
const OW_LT_LI: u8 = 8;
const OW_LT_EMPTY: u8 = 9;
const OW_LT_TH: u8 = 10;
const OW_LT_TD: u8 = 11;
const OW_LT_BQ: u8 = 12;
const OW_LT_BOLD: u8 = 13;
const OW_LT_ITALIC: u8 = 14;
const OW_LT_CODE: u8 = 15;
const OW_LT_IMAGE: u8 = 16;

/* ── output structures — must match ow_html.h exactly ── */
#[repr(C)]
#[derive(Copy, Clone)]
struct OwLink {
    line: c_int,
    sc: c_int,
    ec: c_int,
    url: [u8; OW_URL_MAX],
}

#[repr(C)]
struct OwImage {
    url: [u8; OW_URL_MAX],
    x: c_int,
    y: c_int,
    w: c_int,
    h: c_int,
    loaded: c_int,
    pixmap: *mut core::ffi::c_void,
}

#[repr(C)]
struct OwLineInfo {
    typ: u8,
}

#[repr(C)]
struct OwForm {
    action: [u8; OW_URL_MAX],
    method: u8,
    field_start: u8,
    field_count: u8,
}

#[repr(C)]
struct OwFormField {
    typ: u8,
    form: c_int,
    name: [u8; OW_URL_MAX],
    value: [u8; OW_URL_MAX],
    line: c_int,
    col: c_int,
    width: c_int,
    checked: u8,
    opt_cnt: u8,
    opt_sel: u8,
    opts: [[u8; OW_SELECT_OPT_SZ]; OW_MAX_SELECT_OPTS],
}

/* Globals owned by qt6/panels/ow_html.c — written here, read by the Qt
 * frontend (qt_panels_openweb.cpp). Offsets/types must match. */
extern "C" {
    static mut ow_txt: [[u8; OW_TXT_COLS]; OW_TXT_LINES];
    static mut ow_txt_lines: c_int;
    static mut ow_links: [OwLink; OW_MAX_LINKS];
    static mut ow_link_cnt: c_int;
    static mut ow_images: [OwImage; OW_MAX_IMAGES];
    static mut ow_image_cnt: c_int;
    static mut ow_need_render: c_int;
    static mut ow_line_info: [OwLineInfo; OW_TXT_LINES];
    static mut ow_line_img: [c_int; OW_TXT_LINES];
    static mut ow_page_title: [u8; OW_URL_MAX];
    static mut ow_forms: [OwForm; OW_MAX_FORMS];
    static mut ow_form_cnt: c_int;
    static mut ow_form_fields: [OwFormField; OW_MAX_FIELDS];
    static mut ow_field_cnt: c_int;

    /* Field-value edit cache lives in C (ow_html.c); the renderer delegates
     * identity-check + restore/store to it exactly like the old C renderer. */
    fn ow_fv_restore(f: *mut OwFormField, fi: c_int);
    fn ow_fv_store(f: *const OwFormField, fi: c_int);
}

/* ── small helpers ── */

fn lower(b: u8) -> u8 {
    if (b'A'..=b'Z').contains(&b) {
        b + 32
    } else {
        b
    }
}

/// Copy a NUL-terminated C string view out of a fixed buffer.
fn cstr(buf: &[u8]) -> &[u8] {
    let mut n = 0;
    while n < buf.len() && buf[n] != 0 {
        n += 1;
    }
    &buf[..n]
}

/// Copy `src` (a byte string) into `dst`, NUL-terminated, like ow_strncpy_n().
fn cpy(dst: &mut [u8], src: &[u8]) {
    let mut i = 0;
    while i < src.len() && i < dst.len() - 1 {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = 0;
}

fn parse_int(s: &[u8]) -> i32 {
    let mut v: i32 = 0;
    for &c in s {
        if !(b'0'..=b'9').contains(&c) {
            break;
        }
        v = v * 10 + (c - b'0') as i32;
    }
    v
}

fn utf8_encode(cp: u32, out: &mut [u8; 5]) -> usize {
    if cp < 0x80 {
        out[0] = cp as u8;
        1
    } else if cp < 0x800 {
        out[0] = 0xC0 | (cp >> 6) as u8;
        out[1] = 0x80 | (cp & 0x3F) as u8;
        2
    } else if cp < 0x10000 {
        out[0] = 0xE0 | (cp >> 12) as u8;
        out[1] = 0x80 | ((cp >> 6) & 0x3F) as u8;
        out[2] = 0x80 | (cp & 0x3F) as u8;
        3
    } else {
        out[0] = 0xF0 | (cp >> 18) as u8;
        out[1] = 0x80 | ((cp >> 12) & 0x3F) as u8;
        out[2] = 0x80 | ((cp >> 6) & 0x3F) as u8;
        out[3] = 0x80 | (cp & 0x3F) as u8;
        4
    }
}

/* Named entities (name incl. leading '&', UTF-8 output bytes). Mirrors the
 * ENT[] table from the old C renderer. */
const ENT: &[(&[u8], &[u8])] = &[
    (b"&amp;", b"&"), (b"&lt;", b"<"), (b"&gt;", b">"), (b"&quot;", b"\""),
    (b"&apos;", b"'"), (b"&nbsp;", b" "),
    (b"&copy;", &[0xC2, 0xA9]), (b"&reg;", &[0xC2, 0xAE]),
    (b"&trade;", &[0xE2, 0x84, 0xA2]), (b"&bull;", &[0xE2, 0x80, 0xA2]),
    (b"&middot;", &[0xC2, 0xB7]), (b"&ndash;", &[0xE2, 0x80, 0x93]),
    (b"&mdash;", &[0xE2, 0x80, 0x94]), (b"&hellip;", &[0xE2, 0x80, 0xA6]),
    (b"&larr;", &[0xE2, 0x86, 0x90]), (b"&rarr;", &[0xE2, 0x86, 0x92]),
    (b"&uarr;", &[0xE2, 0x86, 0x91]), (b"&darr;", &[0xE2, 0x86, 0x93]),
    (b"&times;", &[0xC3, 0x97]), (b"&divide;", &[0xC3, 0xB7]),
    (b"&euro;", &[0xE2, 0x82, 0xAC]), (b"&pound;", &[0xC2, 0xA3]),
    (b"&yen;", &[0xC2, 0xA5]), (b"&cent;", &[0xC2, 0xA2]),
    (b"&lsquo;", &[0xE2, 0x80, 0x98]), (b"&rsquo;", &[0xE2, 0x80, 0x99]),
    (b"&ldquo;", &[0xE2, 0x80, 0x9C]), (b"&rdquo;", &[0xE2, 0x80, 0x9D]),
    (b"&laquo;", &[0xC2, 0xAB]), (b"&raquo;", &[0xC2, 0xBB]),
    (b"&lsaquo;", &[0xE2, 0x80, 0xB9]), (b"&rsaquo;", &[0xE2, 0x80, 0xBA]),
    (b"&sbquo;", &[0xE2, 0x80, 0x9A]), (b"&bdquo;", &[0xE2, 0x80, 0x9E]),
    (b"&sect;", &[0xC2, 0xA7]), (b"&para;", &[0xC2, 0xB6]),
    (b"&deg;", &[0xC2, 0xB0]), (b"&plusmn;", &[0xC2, 0xB1]),
    (b"&micro;", &[0xC2, 0xB5]), (b"&sup2;", &[0xC2, 0xB2]),
    (b"&sup3;", &[0xC2, 0xB3]), (b"&frac12;", &[0xC2, 0xBD]),
    (b"&frac14;", &[0xC2, 0xBC]), (b"&frac34;", &[0xC2, 0xBE]),
    (b"&acute;", &[0xC2, 0xB4]), (b"&uml;", &[0xC2, 0xA8]),
    (b"&cedil;", &[0xC2, 0xB8]), (b"&ordm;", &[0xC2, 0xBA]),
    (b"&ordf;", &[0xC2, 0xAA]), (b"&not;", &[0xC2, 0xAC]),
    (b"&brvbar;", &[0xC2, 0xA6]), (b"&iquest;", &[0xC2, 0xBF]),
    (b"&iexcl;", &[0xC2, 0xA1]),
    (b"&spades;", &[0xE2, 0x99, 0xA0]), (b"&clubs;", &[0xE2, 0x99, 0xA3]),
    (b"&hearts;", &[0xE2, 0x99, 0xA5]), (b"&diams;", &[0xE2, 0x99, 0xA6]),
    (b"&infin;", &[0xE2, 0x88, 0x9E]), (b"&ne;", &[0xE2, 0x89, 0xA0]),
    (b"&le;", &[0xE2, 0x89, 0xA4]), (b"&ge;", &[0xE2, 0x89, 0xA5]),
    (b"&agrave;", &[0xC3, 0xA0]), (b"&aacute;", &[0xC3, 0xA1]),
    (b"&acirc;", &[0xC3, 0xA2]), (b"&atilde;", &[0xC3, 0xA3]),
    (b"&auml;", &[0xC3, 0xA4]), (b"&aring;", &[0xC3, 0xA5]),
    (b"&aelig;", &[0xC3, 0xA6]), (b"&ccedil;", &[0xC3, 0xA7]),
    (b"&egrave;", &[0xC3, 0xA8]), (b"&eacute;", &[0xC3, 0xA9]),
    (b"&ecirc;", &[0xC3, 0xAA]), (b"&euml;", &[0xC3, 0xAB]),
    (b"&igrave;", &[0xC3, 0xAC]), (b"&iacute;", &[0xC3, 0xAD]),
    (b"&icirc;", &[0xC3, 0xAE]), (b"&iuml;", &[0xC3, 0xAF]),
    (b"&ntilde;", &[0xC3, 0xB1]), (b"&ograve;", &[0xC3, 0xB2]),
    (b"&oacute;", &[0xC3, 0xB3]), (b"&ocirc;", &[0xC3, 0xB4]),
    (b"&otilde;", &[0xC3, 0xB5]), (b"&ouml;", &[0xC3, 0xB6]),
    (b"&oslash;", &[0xC3, 0xB8]), (b"&ugrave;", &[0xC3, 0xB9]),
    (b"&uacute;", &[0xC3, 0xBA]), (b"&ucirc;", &[0xC3, 0xBB]),
    (b"&uuml;", &[0xC3, 0xBC]), (b"&yacute;", &[0xC3, 0xBD]),
    (b"&szlig;", &[0xC3, 0x9F]),
];

/// Decode the entity starting at `html[i]` ('&'). Returns consumed bytes
/// (0 if it is not an entity); fills out[0..*ol).
fn try_entity(html: &[u8], i: usize, out: &mut [u8; 5]) -> (usize, usize) {
    if i + 2 < html.len() && html[i + 1] == b'#' {
        let mut base: u32 = 10;
        let mut j = i + 2;
        if j < html.len() && (html[j] == b'x' || html[j] == b'X') {
            base = 16;
            j += 1;
        }
        if j >= html.len() {
            return (0, 0);
        }
        let mut cp: u32 = 0;
        let mut digits: u32 = 0;
        while j < html.len() && digits < 6 {
            let c = html[j];
            let v: i32 = if base == 16 {
                if (b'0'..=b'9').contains(&c) {
                    (c - b'0') as i32
                } else if (b'a'..=b'f').contains(&c) {
                    (c - b'a' + 10) as i32
                } else if (b'A'..=b'F').contains(&c) {
                    (c - b'A' + 10) as i32
                } else {
                    -1
                }
            } else if (b'0'..=b'9').contains(&c) {
                (c - b'0') as i32
            } else {
                -1
            };
            if v < 0 {
                break;
            }
            cp = cp * base + v as u32;
            digits += 1;
            j += 1;
        }
        if digits == 0 || j >= html.len() || html[j] != b';' {
            return (0, 0);
        }
        if cp == 0 || cp > 0x10FFFF {
            return (0, 0);
        }
        let ol = utf8_encode(cp, out);
        return (j - i + 1, ol);
    }
    for &(name, bytes) in ENT {
        if html.len() - i >= name.len() && &html[i..i + name.len()] == name {
            out[..bytes.len()].copy_from_slice(bytes);
            return (name.len(), bytes.len());
        }
    }
    (0, 0)
}

/* ── tag matching (mirrors tag_match / tag_match_exact) ── */

/// 1 = <tag ... , 2 = </tag ...>, 0 = no match.
fn tag_match(html: &[u8], pos: usize, tag: &[u8]) -> i32 {
    if pos >= html.len() || html[pos] != b'<' {
        return 0;
    }
    let is_close = pos + 1 < html.len() && html[pos + 1] == b'/';
    let mut p = if is_close { pos + 2 } else { pos + 1 };
    for &t in tag {
        if p >= html.len() || lower(html[p]) != lower(t) {
            return 0;
        }
        p += 1;
    }
    if p >= html.len() {
        return 0;
    }
    let n = html[p];
    if n == b'>' || n == b' ' || n == b'\t' || n == b'/' {
        if is_close {
            2
        } else {
            1
        }
    } else {
        0
    }
}

/// Like tag_match but requires '>' immediately after the name.
fn tag_match_exact(html: &[u8], pos: usize, tag: &[u8]) -> i32 {
    if pos >= html.len() || html[pos] != b'<' {
        return 0;
    }
    let is_close = pos + 1 < html.len() && html[pos + 1] == b'/';
    let mut p = if is_close { pos + 2 } else { pos + 1 };
    for &t in tag {
        if p >= html.len() || lower(html[p]) != lower(t) {
            return 0;
        }
        p += 1;
    }
    if p < html.len() && html[p] == b'>' {
        if is_close {
            2
        } else {
            1
        }
    } else {
        0
    }
}

/// Advance past the '>' closing the tag at `i`.
fn skip_tag_end(html: &[u8], mut i: usize) -> usize {
    while i < html.len() && html[i] != b'>' {
        i += 1;
    }
    if i < html.len() {
        i += 1;
    }
    i
}

/* ── attribute parsing (mirrors parse_attrs) ── */
const MAX_ATTR: usize = 16;

#[derive(Copy, Clone)]
struct Attr {
    name: [u8; 16],
    value: [u8; OW_URL_MAX],
}

fn parse_attrs(html: &[u8], tg_start: usize, attrs: &mut [Attr]) -> usize {
    let mut p = tg_start;
    let mut n = 0usize;
    while p < html.len() && html[p] != b'>' && html[p] != b' ' && html[p] != b'\t'
        && html[p] != b'\n' && html[p] != b'/'
    {
        p += 1;
    }
    while p < html.len() && html[p] != b'>' && n < attrs.len() {
        while p < html.len() && html[p] != b'>'
            && (html[p] == b' ' || html[p] == b'\t' || html[p] == b'\n')
        {
            p += 1;
        }
        if p >= html.len() || html[p] == b'>' || html[p] == b'/' {
            break;
        }
        let mut ni = 0usize;
        while p < html.len() && html[p] != b'>' && html[p] != b'=' && html[p] != b' '
            && html[p] != b'\t' && html[p] != b'\n' && ni < 15
        {
            attrs[n].name[ni] = lower(html[p]);
            ni += 1;
            p += 1;
        }
        attrs[n].name[ni] = 0;
        while p < html.len() && html[p] != b'>'
            && (html[p] == b' ' || html[p] == b'\t' || html[p] == b'\n')
        {
            p += 1;
        }
        let mut vi = 0usize;
        if p < html.len() && html[p] == b'=' {
            p += 1;
            while p < html.len() && html[p] != b'>'
                && (html[p] == b' ' || html[p] == b'\t' || html[p] == b'\n')
            {
                p += 1;
            }
            if p < html.len() && (html[p] == b'"' || html[p] == b'\'') {
                let q = html[p];
                p += 1;
                while p < html.len() && html[p] != q && vi < OW_URL_MAX - 1 {
                    attrs[n].value[vi] = html[p];
                    vi += 1;
                    p += 1;
                }
                if p < html.len() && html[p] == q {
                    p += 1;
                }
            } else {
                while p < html.len() && html[p] != b'>' && html[p] != b' '
                    && html[p] != b'\t' && html[p] != b'\n' && vi < OW_URL_MAX - 1
                {
                    attrs[n].value[vi] = html[p];
                    vi += 1;
                    p += 1;
                }
            }
        }
        attrs[n].value[vi] = 0;
        n += 1;
    }
    n
}

/* ── renderer state ── */

struct St {
    buf: [u8; OW_TXT_COLS],
    col: usize,
    in_a: bool,
    cur_link: i32,
    in_bold: bool,
    in_italic: bool,
    in_code: bool,
    line_bold: bool,
    line_italic: bool,
    line_code: bool,
    cell_type: u8,
    cur_form: i32,
    textarea_slot: i32,
    select_slot: i32,
    in_option: bool,
    opt_sel: bool,
    opt_tmp: [u8; OW_SELECT_OPT_SZ],
    h_level: i32,
    list_type: i32,
    ol_count: i32,
    in_pre: bool,
    in_title: bool,
    in_row: bool,
    cell_count: i32,
    blockquote_depth: i32,
}

impl St {
    fn new() -> Self {
        St {
            buf: [0; OW_TXT_COLS],
            col: 0,
            in_a: false,
            cur_link: -1,
            in_bold: false,
            in_italic: false,
            in_code: false,
            line_bold: false,
            line_italic: false,
            line_code: false,
            cell_type: 0,
            cur_form: -1,
            textarea_slot: -1,
            select_slot: -1,
            in_option: false,
            opt_sel: false,
            opt_tmp: [0; OW_SELECT_OPT_SZ],
            h_level: 0,
            list_type: 0,
            ol_count: 0,
            in_pre: false,
            in_title: false,
            in_row: false,
            cell_count: 0,
            blockquote_depth: 0,
        }
    }

    fn line_set_type(ln: i32, t: u8) {
        if ln >= 0 && (ln as usize) < OW_TXT_LINES {
            unsafe {
                ow_line_info[ln as usize].typ = t;
            }
        }
    }

    fn add_blank_line() {
        unsafe {
            let n = ow_txt_lines as usize;
            if n > 0 && ow_txt[n - 1][0] != 0 {
                if n < OW_TXT_LINES {
                    ow_txt[n][0] = 0;
                    Self::line_set_type(n as i32, OW_LT_EMPTY);
                    ow_txt_lines = (n + 1) as c_int;
                }
            }
        }
    }

    /// Flush the pending line to the grid (mirrors flush_buf). Caller keeps
    /// `self`; resets self.col to 0.
    fn flush_buf(&mut self) {
        if self.col == 0 {
            return;
        }
        if self.in_a && self.cur_link >= 0 && (self.cur_link as usize) < OW_MAX_LINKS {
            unsafe {
                ow_links[self.cur_link as usize].ec = self.col as c_int;
            }
        }
        self.buf[self.col] = 0;
        let ln = unsafe { ow_txt_lines } as usize;
        if ln >= OW_TXT_LINES {
            self.line_bold = false;
            self.line_italic = false;
            self.line_code = false;
            self.col = 0;
            return;
        }
        unsafe {
            let mut j = 0usize;
            while j < OW_TXT_COLS - 1 && self.buf[j] != 0 {
                ow_txt[ln][j] = self.buf[j];
                j += 1;
            }
            ow_txt[ln][j] = 0;
            if ow_line_info[ln].typ == OW_LT_NORMAL {
                if self.cell_type != 0 {
                    ow_line_info[ln].typ = self.cell_type;
                } else if self.line_code {
                    ow_line_info[ln].typ = OW_LT_CODE;
                } else if self.line_bold {
                    ow_line_info[ln].typ = OW_LT_BOLD;
                } else if self.line_italic {
                    ow_line_info[ln].typ = OW_LT_ITALIC;
                }
            }
            /* continue a spanning anchor onto the new line */
            if self.in_a
                && self.cur_link >= 0
                && (self.cur_link as usize) < OW_MAX_LINKS
                && ow_link_cnt < OW_MAX_LINKS as c_int
            {
                let src = self.cur_link as usize;
                let dst = ow_link_cnt as usize;
                ow_links[dst] = ow_links[src];
                ow_links[dst].line = (ln + 1) as c_int;
                ow_links[dst].sc = 0;
                ow_links[dst].ec = 0;
                self.cur_link = ow_link_cnt;
                ow_link_cnt += 1;
            }
            ow_txt_lines = (ln + 1) as c_int;
        }
        self.line_bold = false;
        self.line_italic = false;
        self.line_code = false;
        self.col = 0;
    }

    fn buf_put(&mut self, s: &[u8]) {
        let mut i = 0usize;
        while i < s.len() {
            /* If we've reached the column boundary, flush the current line */
            if self.col >= OW_TXT_COLS - 1 {
                self.flush_buf();
            }
            self.buf[self.col] = s[i];
            self.col += 1;
            i += 1;
        }
    }

    fn field_type_id(t: &[u8]) -> i32 {
        if t.is_empty() {
            return OW_FT_TEXT as i32;
        }
        if t == b"password" {
            return OW_FT_PASSWORD as i32;
        }
        if t == b"submit" {
            return OW_FT_SUBMIT as i32;
        }
        if t == b"button" {
            return OW_FT_BUTTON as i32;
        }
        if t == b"checkbox" {
            return OW_FT_CHECKBOX as i32;
        }
        if t == b"radio" {
            return OW_FT_RADIO as i32;
        }
        if t == b"hidden" || t == b"file" || t == b"image" {
            return -1;
        }
        OW_FT_TEXT as i32
    }

    /// Render an `<input>`-style box into the pending line (mirrors the C
    /// render_field_box(): checkbox/radio → "[x]"/"[ ]", submit/button →
    /// centered "[ label ]", select → "[ value v ]", text/password →
    /// "[ value ]" with password masking).
    fn render_field_box(&mut self, f: &mut OwFormField) {
        let start = self.col;
        if start >= OW_TXT_COLS - 1 {
            return;
        }
        if start > 0 && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
        }
        f.line = unsafe { ow_txt_lines };
        f.col = self.col as c_int;

        let vlen = cstr(&f.value).len();
        let src = cstr(&f.value);
        let mut center = false;
        let mut w: usize;
        match f.typ {
            OW_FT_CHECKBOX | OW_FT_RADIO => {
                self.buf_put(if f.checked != 0 { b"[x]" } else { b"[ ]" });
                f.width = 3;
                return;
            }
            OW_FT_SUBMIT | OW_FT_BUTTON => {
                w = vlen + 4;
                center = true;
            }
            OW_FT_SELECT => {
                w = vlen + 4;
            }
            _ => {
                w = vlen + 2;
                if w < 8 {
                    w = 8;
                }
                if w > 24 {
                    w = 24;
                }
            }
        }
        if w > OW_TXT_COLS - 1 - self.col {
            w = OW_TXT_COLS - 1 - self.col;
        }
        if w < 2 {
            w = 2;
        }
        self.buf_put(b"[");
        let inner = w - 2;
        if center {
            let mut pad = (inner as i32 - vlen as i32) / 2;
            if pad < 0 {
                pad = 0;
            }
            for _ in 0..(pad as usize) {
                if self.col < OW_TXT_COLS - 1 {
                    self.buf[self.col] = b' ';
                    self.col += 1;
                }
            }
        }
        let mut k = 0usize;
        let is_pw = f.typ == OW_FT_PASSWORD;
        while k < src.len() && k < inner && self.col < OW_TXT_COLS - 1 {
            let c = if is_pw && src[k] != b' ' { b'*' } else { src[k] };
            self.buf[self.col] = c;
            self.col += 1;
            k += 1;
        }
        while self.col < start + 1 + inner && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
        }
        if f.typ == OW_FT_SELECT && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b'>';
            self.col += 1;
        }
        if f.typ == OW_FT_SELECT && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
        }
        while self.col < start + w - 1 && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
        }
        if self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b']';
            self.col += 1;
        }
        f.width = self.col as c_int - f.col;
    }

    /// Two-row textarea box (mirrors render_textarea_box).
    fn render_textarea_box(&mut self, f: &mut OwFormField) {
        if self.col > 0 && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
        }
        f.line = unsafe { ow_txt_lines };
        f.col = self.col as c_int;
        let mut w: usize = 36;
        self.buf_put(b"(");
        let src = cstr(&f.value);
        let mut k = 0usize;
        while k < src.len() && k < w - 4 && self.col < OW_TXT_COLS - 2 {
            if src[k] == b'\n' || src[k] == b'\r' {
                break;
            }
            self.buf[self.col] = src[k];
            self.col += 1;
            k += 1;
        }
        while self.col < f.col as usize + w - 1 && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
        }
        if self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b')';
            self.col += 1;
        }
        if self.col - f.col as usize > w {
            w = self.col - f.col as usize;
        }
        self.flush_buf();
        if self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b'(';
            self.col += 1;
        }
        let mut k = 0usize;
        while k < w - 2 && self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b' ';
            self.col += 1;
            k += 1;
        }
        if self.col < OW_TXT_COLS - 1 {
            self.buf[self.col] = b')';
            self.col += 1;
        }
        f.width = w as c_int;
    }
}

/// Add one <option> to the open select (mirrors the C opt_add()).
fn opt_add(fi: usize, txt: &[u8], selected: bool) {
    unsafe {
        let f = &mut ow_form_fields[fi];
        if f.opt_cnt as usize >= OW_MAX_SELECT_OPTS {
            return;
        }
        let idx = f.opt_cnt as usize;
        cpy(&mut f.opts[idx], txt);
        if selected {
            f.opt_sel = f.opt_cnt;
            f.value[0] = 0;
        }
        if f.opt_sel == f.opt_cnt && f.value[0] == 0 {
            cpy(&mut f.value, cstr(&f.opts[idx]));
        }
        f.opt_cnt += 1;
    }
}

/* ── the renderer ── */

/// Render `html[0..len]` into the shared C-owned grid + link/image/form
/// structures. Mirrors the old C `render_html()`.
#[no_mangle]
pub extern "C" fn ow_render_rs(html: *const c_char, len: c_int) {
    if html.is_null() || len <= 0 {
        return;
    }
    let html: &[u8] = unsafe { core::slice::from_raw_parts(html as *const u8, len as usize) };

    /* reset output structures */
    unsafe {
        for ti in 0..OW_TXT_LINES {
            ow_txt[ti][0] = 0;
            ow_line_info[ti].typ = OW_LT_NORMAL;
            ow_line_img[ti] = -1;
        }
        ow_txt_lines = 0;
        ow_link_cnt = 0;
        ow_image_cnt = 0;
        ow_page_title[0] = 0;
        ow_form_cnt = 0;
        ow_field_cnt = 0;
    }

    let mut st = St::new();

    let mut i = 0usize;
    while i < html.len() {
        let lines = unsafe { ow_txt_lines } as usize;
        if lines >= OW_TXT_LINES {
            break;
        }
        if html[i] == b'<' {
            /* ── comments / doctype / CDATA ── */
            if i + 3 < html.len() && &html[i..i + 4] == b"<!--" {
                let mut j = i + 4;
                while j + 2 < html.len()
                    && !(html[j] == b'-' && html[j + 1] == b'-' && html[j + 2] == b'>')
                {
                    j += 1;
                }
                i = if j + 2 < html.len() { j + 3 } else { html.len() };
                continue;
            }
            if i + 1 < html.len() && (html[i + 1] == b'!' || html[i + 1] == b'?') {
                let mut j = i + 2;
                if html[i + 1] == b'!' && i + 8 < html.len() && &html[i..i + 9] == b"<![CDATA[" {
                    while j + 2 < html.len()
                        && !(html[j] == b']' && html[j + 1] == b']' && html[j + 2] == b'>')
                    {
                        j += 1;
                    }
                    i = if j + 2 < html.len() { j + 3 } else { html.len() };
                } else {
                    while j < html.len() && html[j] != b'>' {
                        j += 1;
                    }
                    i = if j < html.len() { j + 1 } else { html.len() };
                }
                continue;
            }

            /* ── <title> capture ── */
            if tag_match_exact(html, i, b"title") == 1 {
                st.in_title = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"title") == 2 {
                st.in_title = false;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"head") != 0
                || tag_match(html, i, b"base") == 1
                || tag_match(html, i, b"meta") == 1
                || tag_match(html, i, b"link") == 1
                || tag_match(html, i, b"source") == 1
                || tag_match(html, i, b"wbr") == 1
            {
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── headings ── */
            let hn: i32 = if tag_match(html, i, b"h1") == 1 {
                1
            } else if tag_match(html, i, b"h2") == 1 {
                2
            } else if tag_match(html, i, b"h3") == 1 {
                3
            } else if tag_match(html, i, b"h4") == 1 {
                4
            } else if tag_match(html, i, b"h5") == 1 {
                5
            } else if tag_match(html, i, b"h6") == 1 {
                6
            } else {
                0
            };
            if hn != 0 {
                st.flush_buf();
                St::add_blank_line();
                st.h_level = hn;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"h1") == 2 || tag_match(html, i, b"h2") == 2
                || tag_match(html, i, b"h3") == 2 || tag_match(html, i, b"h4") == 2
                || tag_match(html, i, b"h5") == 2 || tag_match(html, i, b"h6") == 2
            {
                st.flush_buf();
                let n = unsafe { ow_txt_lines };
                if n > 0 {
                    let t = match st.h_level {
                        1 => OW_LT_H1,
                        2 => OW_LT_H2,
                        3 => OW_LT_H3,
                        4 => OW_LT_H4,
                        5 => OW_LT_H5,
                        _ => OW_LT_H6,
                    };
                    St::line_set_type(n - 1, t);

                    /* Add underline decoration for H1–H3 */
                    if st.h_level >= 1 && st.h_level <= 3 {
                        unsafe {
                            let ln_idx = (n - 1) as usize;
                            /* Measure heading text width (scan to NUL) */
                            let mut tw: usize = 0;
                            while tw < OW_TXT_COLS - 1 && ow_txt[ln_idx][tw] != 0 {
                                tw += 1;
                            }
                            /* Trim trailing spaces */
                            while tw > 0 && ow_txt[ln_idx][tw - 1] == b' ' { tw -= 1; }
                            if tw > 0 && (n as usize) < OW_TXT_LINES {
                                let uln = n as usize;
                                let ub: u8 = match st.h_level {
                                    1 => b'=',
                                    2 => b'-',
                                    _ => b'.',
                                };
                                let mut k = 0usize;
                                while k < tw && k < OW_TXT_COLS - 1 {
                                    ow_txt[uln][k] = ub;
                                    k += 1;
                                }
                                ow_txt[uln][k] = 0;
                                St::line_set_type(uln as i32, t);
                                ow_txt_lines = (n + 1) as c_int;
                            }
                        }
                    }
                }
                St::add_blank_line();
                st.h_level = 0;
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── paragraph ── */
            if tag_match(html, i, b"p") == 1 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"p") == 2 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── line break ── */
            if tag_match(html, i, b"br") != 0 {
                st.flush_buf();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── lists ── */
            if tag_match(html, i, b"ul") == 1 {
                st.list_type = 1;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"ol") == 1 {
                st.list_type = 2;
                st.ol_count = 0;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"ul") == 2 || tag_match(html, i, b"ol") == 2 {
                st.flush_buf();
                St::add_blank_line();
                st.list_type = 0;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"li") == 1 {
                st.flush_buf();
                if st.list_type == 2 {
                    st.ol_count += 1;
                }
                if st.list_type == 1 {
                    st.buf_put(b"\xE2\x80\xA2 ");
                } else if st.list_type == 2 {
                    let mut num = [0u8; 8];
                    let mut ni = 0usize;
                    let mut n = st.ol_count;
                    while n > 0 {
                        num[ni] = b'0' + (n % 10) as u8;
                        n /= 10;
                        ni += 1;
                    }
                    while ni > 0 && st.col < OW_TXT_COLS - 1 {
                        ni -= 1;
                        st.buf[st.col] = num[ni];
                        st.col += 1;
                    }
                    st.buf_put(b". ");
                } else {
                    st.buf_put(b"* ");
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"li") == 2 {
                st.flush_buf();
                let n = unsafe { ow_txt_lines };
                if n > 0 {
                    St::line_set_type(n - 1, OW_LT_LI);
                }
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── horizontal rule ── */
            if tag_match(html, i, b"hr") != 0 {
                st.flush_buf();
                unsafe {
                    let ln = ow_txt_lines as usize;
                    if ln < OW_TXT_LINES {
                        let mut k = 0usize;
                        while k < OW_TXT_COLS - 1 {
                            ow_txt[ln][k] = 0xE2;
                            k += 1;
                            if k < OW_TXT_COLS - 1 { ow_txt[ln][k] = 0x94; k += 1; }
                            if k < OW_TXT_COLS - 1 { ow_txt[ln][k] = 0x80; k += 1; }
                        }
                        ow_txt[ln][OW_TXT_COLS - 1] = 0;
                        St::line_set_type(ln as i32, OW_LT_HR);
                        ow_txt_lines = (ln + 1) as c_int;
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── blockquote ── */
            if tag_match_exact(html, i, b"blockquote") == 1 {
                st.flush_buf();
                st.blockquote_depth += 1;
                St::add_blank_line();
                unsafe {
                    let ln = ow_txt_lines as usize;
                    if ln < OW_TXT_LINES {
                        /* Prefix with one `>` per nesting level */
                        let depth = st.blockquote_depth as usize;
                        let mut li = 0usize;
                        while li < depth && li * 2 < OW_TXT_COLS - 1 {
                            ow_txt[ln][li * 2] = b'>';
                            ow_txt[ln][li * 2 + 1] = b' ';
                            li += 1;
                        }
                        ow_txt[ln][li * 2] = 0;
                        let mut pad = li * 2;
                        while pad < OW_TXT_COLS - 1 {
                            ow_txt[ln][pad] = b' ';
                            pad += 1;
                        }
                        ow_txt[ln][OW_TXT_COLS - 1] = 0;
                        St::line_set_type(ln as i32, OW_LT_BQ);
                        ow_txt_lines = (ln + 1) as c_int;
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"blockquote") == 2 {
                st.flush_buf();
                if st.blockquote_depth > 0 { st.blockquote_depth -= 1; }
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── code (inline) ── */
            if tag_match(html, i, b"code") == 1 && !st.in_pre {
                st.in_code = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"code") == 2 && !st.in_pre {
                st.in_code = false;
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── address (italic) ── */
            if tag_match(html, i, b"address") == 1 {
                st.flush_buf();
                St::add_blank_line();
                st.in_italic = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"address") == 2 {
                st.in_italic = false;
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── figcaption / legend / caption ── */
            if tag_match(html, i, b"figcaption") == 1 || tag_match(html, i, b"legend") == 1
                || tag_match(html, i, b"caption") == 1
            {
                st.flush_buf();
                st.in_italic = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"figcaption") == 2 || tag_match(html, i, b"legend") == 2
                || tag_match(html, i, b"caption") == 2
            {
                st.in_italic = false;
                st.flush_buf();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── definition lists ── */
            if tag_match(html, i, b"dl") == 1 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"dl") == 2 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"dt") == 1 {
                st.flush_buf();
                st.in_bold = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"dt") == 2 {
                st.in_bold = false;
                st.flush_buf();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"dd") == 1 {
                st.flush_buf();
                st.buf_put(b"    ");
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"dd") == 2 {
                st.flush_buf();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── details / summary ── */
            if tag_match(html, i, b"details") == 1 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"details") == 2 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"summary") == 1 {
                st.flush_buf();
                st.buf_put(b"\xE2\x96\xB8 ");
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"summary") == 2 {
                st.flush_buf();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── generic block tags: paragraph boundaries ── */
            const BLOCK_TAGS: &[&[u8]] = &[
                b"div", b"section", b"article", b"aside", b"header", b"footer", b"nav",
                b"main", b"center", b"fieldset", b"figure", b"hgroup",
            ];
            let mut is_block = false;
            for &t in BLOCK_TAGS {
                if tag_match(html, i, t) != 0 {
                    is_block = true;
                    break;
                }
            }
            if is_block {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── text formatting ── */
            if tag_match(html, i, b"b") == 1 || tag_match(html, i, b"strong") == 1 {
                st.in_bold = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"b") == 2 || tag_match(html, i, b"strong") == 2 {
                st.in_bold = false;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"i") == 1 || tag_match(html, i, b"em") == 1 {
                st.in_italic = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"i") == 2 || tag_match(html, i, b"em") == 2 {
                st.in_italic = false;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"q") == 1 {
                st.buf_put(b"\xE2\x80\x9C");
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"q") == 2 {
                st.buf_put(b"\xE2\x80\x9D");
                i = skip_tag_end(html, i);
                continue;
            }
            const INLINE_TAGS: &[&[u8]] = &[
                b"u", b"s", b"strike", b"mark", b"small", b"big", b"sub", b"sup", b"abbr",
                b"cite", b"var", b"dfn", b"time", b"acronym", b"tt", b"kbd", b"samp", b"span",
            ];
            let mut is_inline = false;
            for &t in INLINE_TAGS {
                if tag_match(html, i, t) != 0 {
                    is_inline = true;
                    break;
                }
            }
            if is_inline {
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── table tags ── */
            if tag_match(html, i, b"table") == 1 {
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"table") == 2 {
                st.flush_buf();
                St::add_blank_line();
                st.in_row = false;
                st.cell_count = 0;
                st.cell_type = 0;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"tr") == 1 {
                st.flush_buf();
                st.in_row = true;
                st.cell_count = 0;
                st.cell_type = 0;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"tr") == 2 {
                st.flush_buf();
                st.in_row = false;
                st.cell_type = 0;
                /* Add row separator line for visual clarity */
                unsafe {
                    let ln = ow_txt_lines as usize;
                    if ln < OW_TXT_LINES && st.cell_count > 0 {
                        let mut k = 0usize;
                        while k < OW_TXT_COLS - 1 {
                            ow_txt[ln][k] = b'-';
                            k += 1;
                        }
                        ow_txt[ln][k] = 0;
                        St::line_set_type(ln as i32, OW_LT_NORMAL);
                        ow_txt_lines = (ln + 1) as c_int;
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"th") == 1 || tag_match(html, i, b"td") == 1 {
                if st.in_row {
                    if st.cell_count > 0 {
                        st.buf_put(b" | ");
                    }
                    st.cell_count += 1;
                }
                st.cell_type = if tag_match(html, i, b"th") == 1 {
                    OW_LT_TH
                } else {
                    OW_LT_TD
                };
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"th") == 2 || tag_match(html, i, b"td") == 2 {
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── image ── */
            if tag_match(html, i, b"img") == 1 {
                unsafe {
                    if ow_image_cnt < OW_MAX_IMAGES as c_int {
                        let img_idx = ow_image_cnt as usize;
                        ow_images[img_idx] = OwImage {
                            url: [0; OW_URL_MAX],
                            x: 0,
                            y: 0,
                            w: 0,
                            h: 0,
                            loaded: 0,
                            pixmap: core::ptr::null_mut(),
                        };
                        let mut alt = [0u8; OW_TXT_COLS];
                        let mut attrs = [Attr {
                            name: [0; 16],
                            value: [0; OW_URL_MAX],
                        }; MAX_ATTR];
                        let na = parse_attrs(html, i + 1, &mut attrs);
                        for k in 0..na {
                            let (name, value) = (
                                cstr(&attrs[k].name),
                                cstr(&attrs[k].value),
                            );
                            if name == b"src" {
                                cpy(&mut ow_images[img_idx].url, value);
                            } else if name == b"width" {
                                ow_images[img_idx].w = parse_int(value);
                            } else if name == b"height" {
                                ow_images[img_idx].h = parse_int(value);
                            } else if name == b"alt" && !value.is_empty() {
                                let mut w = value.len();
                                if w > 36 { w = 36; }
                                alt[..w].copy_from_slice(&value[..w]);
                                alt[w] = 0;
                            }
                        }
                        st.flush_buf();
                        let ln = ow_txt_lines as usize;
                        if ln < OW_TXT_LINES {
                            St::line_set_type(ln as i32, OW_LT_IMAGE);
                            let mut ai = 0usize;
                            if alt[0] != 0 {
                                st_img_mark(&mut ow_txt[ln], &mut ai, b"[\xE2\x9C\x89] ");
                                let mut bi = 0usize;
                                while alt[bi] != 0 && ai < OW_TXT_COLS - 1 && bi < alt.len() {
                                    ow_txt[ln][ai] = alt[bi];
                                    ai += 1;
                                    bi += 1;
                                }
                                /* Append dimensions if available */
                                let iw = ow_images[img_idx].w;
                                let ih = ow_images[img_idx].h;
                                if iw > 0 && ih > 0 && ai + 8 < OW_TXT_COLS {
                                    ow_txt[ln][ai] = b' ';
                                    ai += 1;
                                    ow_txt[ln][ai] = b'(';
                                    ai += 1;
                                    let mut tmp = iw;
                                    let mut dig = [0u8; 6];
                                    let mut nd = 0;
                                    if tmp == 0 { dig[0] = b'0'; nd = 1; }
                                    else { while tmp > 0 && nd < 6 { dig[nd] = b'0' + (tmp % 10) as u8; tmp /= 10; nd += 1; } }
                                    while nd > 0 && ai < OW_TXT_COLS - 4 { nd -= 1; ow_txt[ln][ai] = dig[nd]; ai += 1; }
                                    ow_txt[ln][ai] = b'x';
                                    ai += 1;
                                    tmp = ih;
                                    nd = 0;
                                    if tmp == 0 { dig[0] = b'0'; nd = 1; }
                                    else { while tmp > 0 && nd < 6 { dig[nd] = b'0' + (tmp % 10) as u8; tmp /= 10; nd += 1; } }
                                    while nd > 0 && ai < OW_TXT_COLS - 2 { nd -= 1; ow_txt[ln][ai] = dig[nd]; ai += 1; }
                                    ow_txt[ln][ai] = b')';
                                    ai += 1;
                                }
                            } else {
                                st_img_mark(&mut ow_txt[ln], &mut ai, b"[\xE2\x9C\x89]");
                            }
                            ow_txt[ln][ai] = 0;
                            ow_line_img[ln] = img_idx as c_int;
                            ow_txt_lines = (ln + 1) as c_int;
                        }
                        ow_image_cnt += 1;
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── anchor ── */
            if !st.in_a && tag_match(html, i, b"a") == 1 {
                unsafe {
                    if ow_link_cnt < OW_MAX_LINKS as c_int {
                        let mut attrs = [Attr {
                            name: [0; 16],
                            value: [0; OW_URL_MAX],
                        }; 8];
                        let na = parse_attrs(html, i + 1, &mut attrs);
                        let mut href: Option<&[u8]> = None;
                        for k in 0..na {
                            if cstr(&attrs[k].name) == b"href" {
                                href = Some(cstr(&attrs[k].value));
                                break;
                            }
                        }
                        if let Some(h) = href {
                            if !h.is_empty() {
                                let li = ow_link_cnt as usize;
                                st.cur_link = ow_link_cnt;
                                cpy(&mut ow_links[li].url, h);
                                ow_links[li].line = -1;
                                ow_links[li].sc = 0;
                                ow_links[li].ec = 0;
                                ow_link_cnt += 1;
                                st.in_a = true;
                            }
                        }
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if st.in_a && tag_match(html, i, b"a") == 2 {
                st.in_a = false;
                st.cur_link = -1;
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── pre ── */
            if tag_match(html, i, b"pre") == 1 {
                st.in_pre = true;
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match_exact(html, i, b"pre") == 2 {
                st.flush_buf();
                St::add_blank_line();
                st.in_pre = false;
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── forms ── */
            if tag_match(html, i, b"form") == 1 {
                unsafe {
                    if ow_form_cnt < OW_MAX_FORMS as c_int {
                        let fi = ow_form_cnt as usize;
                        ow_forms[fi] = OwForm {
                            action: [0; OW_URL_MAX],
                            method: 0,
                            field_start: ow_field_cnt as u8,
                            field_count: 0,
                        };
                        let mut attrs = [Attr {
                            name: [0; 16],
                            value: [0; OW_URL_MAX],
                        }; 8];
                        let na = parse_attrs(html, i + 1, &mut attrs);
                        for k in 0..na {
                            let (name, value) = (
                                cstr(&attrs[k].name),
                                cstr(&attrs[k].value),
                            );
                            if name == b"action" {
                                cpy(&mut ow_forms[fi].action, value);
                            } else if name == b"method"
                                && !value.is_empty()
                                && (value[0] == b'p' || value[0] == b'P')
                            {
                                ow_forms[fi].method = 1; /* OW_FM_POST */
                            }
                        }
                        st.cur_form = ow_form_cnt;
                        ow_form_cnt += 1;
                        st.flush_buf();
                        St::add_blank_line();
                    } else {
                        st.cur_form = -1;
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"form") == 2 {
                unsafe {
                    if st.cur_form >= 0 && (st.cur_form as usize) < OW_MAX_FORMS {
                        let fi = st.cur_form as usize;
                        let fs = ow_forms[fi].field_start as c_int;
                        ow_forms[fi].field_count = (ow_field_cnt - fs) as u8;
                    }
                }
                st.cur_form = -1;
                st.flush_buf();
                St::add_blank_line();
                i = skip_tag_end(html, i);
                continue;
            }

            /* <input> */
            if tag_match(html, i, b"input") == 1 {
                unsafe {
                    let mut attrs = [Attr {
                        name: [0; 16],
                        value: [0; OW_URL_MAX],
                    }; 12];
                    let na = parse_attrs(html, i + 1, &mut attrs);
                    let mut fname = [0u8; OW_URL_MAX];
                    let mut fval = [0u8; OW_URL_MAX];
                    let mut ftype = [0u8; 16];
                    let mut checked = false;
                    for k in 0..na {
                        let (name, value) = (cstr(&attrs[k].name), &attrs[k].value);
                        if name == b"name" {
                            cpy(&mut fname, cstr(value));
                        } else if name == b"value" {
                            cpy(&mut fval, cstr(value));
                        } else if name == b"type" {
                            let v = cstr(value);
                            let mut t = 0usize;
                            while t < v.len() && t < 15 {
                                ftype[t] = lower(v[t]);
                                t += 1;
                            }
                            ftype[t] = 0;
                        } else if name == b"checked" {
                            checked = true;
                        }
                    }
                    let tid = St::field_type_id(cstr(&ftype));
                    if tid >= 0 && ow_field_cnt < OW_MAX_FIELDS as c_int {
                        let fi = ow_field_cnt as usize;
                        ow_form_fields[fi] = OwFormField {
                            typ: tid as u8,
                            form: st.cur_form,
                            name: fname,
                            value: fval,
                            line: 0,
                            col: 0,
                            width: 0,
                            checked: checked as u8,
                            opt_cnt: 0,
                            opt_sel: 0,
                            opts: [[0; OW_SELECT_OPT_SZ]; OW_MAX_SELECT_OPTS],
                        };
                        ow_field_cnt = (fi + 1) as c_int;
                        ow_fv_restore(&mut ow_form_fields[fi], fi as c_int);
                        st.render_field_box(&mut ow_form_fields[fi]);
                        ow_fv_store(&ow_form_fields[fi], fi as c_int);
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }

            /* <button type=submit|button> */
            if tag_match(html, i, b"button") == 1 {
                unsafe {
                    let mut attrs = [Attr {
                        name: [0; 16],
                        value: [0; OW_URL_MAX],
                    }; 8];
                    let na = parse_attrs(html, i + 1, &mut attrs);
                    let mut fname = [0u8; OW_URL_MAX];
                    let mut fval = [0u8; OW_URL_MAX];
                    let mut is_submit: i32 = 1;
                    for k in 0..na {
                        let (name, value) = (cstr(&attrs[k].name), cstr(&attrs[k].value));
                        if name == b"name" {
                            cpy(&mut fname, value);
                        } else if name == b"value" {
                            cpy(&mut fval, value);
                        } else if name == b"type" && value == b"reset" {
                            is_submit = -1;
                        } else if name == b"type" && value != b"submit" {
                            is_submit = 0;
                        }
                    }
                    if is_submit >= 0 && ow_field_cnt < OW_MAX_FIELDS as c_int {
                        let fi = ow_field_cnt as usize;
                        let mut value = fval;
                        if value[0] == 0 {
                            cpy(&mut value, b"Submit");
                        }
                        ow_form_fields[fi] = OwFormField {
                            typ: if is_submit > 0 { OW_FT_SUBMIT } else { OW_FT_BUTTON },
                            form: st.cur_form,
                            name: fname,
                            value,
                            line: 0,
                            col: 0,
                            width: 0,
                            checked: 0,
                            opt_cnt: 0,
                            opt_sel: 0,
                            opts: [[0; OW_SELECT_OPT_SZ]; OW_MAX_SELECT_OPTS],
                        };
                        ow_field_cnt = (fi + 1) as c_int;
                        ow_fv_restore(&mut ow_form_fields[fi], fi as c_int);
                        st.render_field_box(&mut ow_form_fields[fi]);
                        ow_fv_store(&ow_form_fields[fi], fi as c_int);
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }

            /* <textarea> */
            if tag_match(html, i, b"textarea") == 1 {
                unsafe {
                    st.textarea_slot = ow_field_cnt;
                    if (st.textarea_slot as usize) < OW_MAX_FIELDS {
                        let fi = st.textarea_slot as usize;
                        ow_form_fields[fi] = OwFormField {
                            typ: OW_FT_TEXTAREA,
                            form: st.cur_form,
                            name: [0; OW_URL_MAX],
                            value: [0; OW_URL_MAX],
                            line: 0,
                            col: 0,
                            width: 0,
                            checked: 0,
                            opt_cnt: 0,
                            opt_sel: 0,
                            opts: [[0; OW_SELECT_OPT_SZ]; OW_MAX_SELECT_OPTS],
                        };
                        let mut attrs = [Attr {
                            name: [0; 16],
                            value: [0; OW_URL_MAX],
                        }; 8];
                        let na = parse_attrs(html, i + 1, &mut attrs);
                        for k in 0..na {
                            if cstr(&attrs[k].name) == b"name" {
                                cpy(&mut ow_form_fields[fi].name, cstr(&attrs[k].value));
                            }
                        }
                        ow_field_cnt = (fi + 1) as c_int;
                    } else {
                        st.textarea_slot = -1;
                    }
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"textarea") == 2 {
                let fi = st.textarea_slot;
                if fi >= 0 {
                    unsafe {
                        let f = &mut ow_form_fields[fi as usize];
                        let mut l = cstr(&f.value).len();
                        while l > 0
                            && (f.value[l - 1] == b' ' || f.value[l - 1] == b'\n'
                                || f.value[l - 1] == b'\r' || f.value[l - 1] == b'\t')
                        {
                            l -= 1;
                        }
                        f.value[l] = 0;
                        ow_fv_restore(f, fi);
                        st.render_textarea_box(f);
                        ow_fv_store(&*f, fi);
                    }
                }
                st.textarea_slot = -1;
                i = skip_tag_end(html, i);
                continue;
            }

            /* <select>/<option> */
            if tag_match(html, i, b"select") == 1 {
                unsafe {
                    st.select_slot = ow_field_cnt;
                    if (st.select_slot as usize) < OW_MAX_FIELDS {
                        let fi = st.select_slot as usize;
                        ow_form_fields[fi] = OwFormField {
                            typ: OW_FT_SELECT,
                            form: st.cur_form,
                            name: [0; OW_URL_MAX],
                            value: [0; OW_URL_MAX],
                            line: 0,
                            col: 0,
                            width: 0,
                            checked: 0,
                            opt_cnt: 0,
                            opt_sel: 0,
                            opts: [[0; OW_SELECT_OPT_SZ]; OW_MAX_SELECT_OPTS],
                        };
                        let mut attrs = [Attr {
                            name: [0; 16],
                            value: [0; OW_URL_MAX],
                        }; 8];
                        let na = parse_attrs(html, i + 1, &mut attrs);
                        for k in 0..na {
                            if cstr(&attrs[k].name) == b"name" {
                                cpy(&mut ow_form_fields[fi].name, cstr(&attrs[k].value));
                            }
                        }
                        ow_field_cnt = (fi + 1) as c_int;
                    } else {
                        st.select_slot = -1;
                    }
                    st.in_option = false;
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"option") == 1 {
                if st.select_slot >= 0 {
                    let mut attrs = [Attr {
                        name: [0; 16],
                        value: [0; OW_URL_MAX],
                    }; 4];
                    let na = parse_attrs(html, i + 1, &mut attrs);
                    st.opt_sel = false;
                    for k in 0..na {
                        if cstr(&attrs[k].name) == b"selected" {
                            st.opt_sel = true;
                        }
                    }
                    st.opt_tmp = [0; OW_SELECT_OPT_SZ];
                    st.in_option = true;
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"option") == 2 {
                if st.select_slot >= 0 && st.in_option {
                    st.in_option = false;
                    let mut l = cstr(&st.opt_tmp).len();
                    while l > 0 && (st.opt_tmp[l - 1] == b' ' || st.opt_tmp[l - 1] == b'\t') {
                        l -= 1;
                    }
                    st.opt_tmp[l] = 0;
                    if st.opt_tmp[0] != 0 {
                        let slot = st.select_slot as usize;
                        let pl = cstr(&st.opt_tmp).len();
                        opt_add(slot, &st.opt_tmp[..pl], st.opt_sel);
                    }
                    st.opt_sel = false;
                }
                i = skip_tag_end(html, i);
                continue;
            }
            if tag_match(html, i, b"select") == 2 {
                let fi = st.select_slot;
                if fi >= 0 {
                    unsafe {
                        let f = &mut ow_form_fields[fi as usize];
                        ow_fv_restore(f, fi);
                        st.render_field_box(f);
                        ow_fv_store(&*f, fi);
                    }
                }
                st.select_slot = -1;
                st.in_option = false;
                i = skip_tag_end(html, i);
                continue;
            }

            /* ── skip containers whose text is not site content ── */
            const SKIP_TAGS: &[&[u8]] = &[
                b"script", b"style", b"noscript", b"svg", b"audio", b"video", b"iframe",
                b"canvas", b"map", b"picture", b"template", b"object", b"select",
                b"optgroup", b"datalist", b"rp", b"rt", b"math",
            ];
            let mut skipped = false;
            for &t in SKIP_TAGS {
                if tag_match(html, i, t) == 1 {
                    skipped = true;
                    let clen = t.len();
                    while i < html.len() {
                        if i + 2 + clen <= html.len()
                            && html[i] == b'<'
                            && html[i + 1] == b'/'
                            && &html[i + 2..i + 2 + clen] == t
                        {
                            i = skip_tag_end(html, i);
                            break;
                        }
                        i += 1;
                    }
                    if i > html.len() {
                        i = html.len();
                    }
                    break;
                }
            }
            if skipped {
                continue;
            }

            /* skip all other tags */
            i = skip_tag_end(html, i);
            continue;
        }

        /* ── form control value capture ── */
        if st.in_option {
            let ot = cstr(&st.opt_tmp).len();
            if html[i] != b'<' && html[i] >= b' ' && ot < OW_SELECT_OPT_SZ - 1 {
                st.opt_tmp[ot] = html[i];
                st.opt_tmp[ot + 1] = 0;
            }
            i += 1;
            continue;
        }
        if st.select_slot >= 0 {
            i += 1;
            continue;
        }
        if st.textarea_slot >= 0 {
            if html[i] != b'<' {
                unsafe {
                    let f = &mut ow_form_fields[st.textarea_slot as usize];
                    let vl = cstr(&f.value).len();
                    if vl < OW_URL_MAX - 1 {
                        f.value[vl] = html[i];
                        f.value[vl + 1] = 0;
                    }
                }
            }
            i += 1;
            continue;
        }

        /* ── entities ── */
        if html[i] == b'&' {
            let mut eout = [0u8; 5];
            let (consumed, el) = try_entity(html, i, &mut eout);
            if consumed > 0 && el <= 5 {
                st.buf_put(&eout[..el]);
                i += consumed;
                continue;
            }
        }

        /* ── title ── */
        if st.in_title {
            if html[i] >= b' ' && html[i] != b'<' {
                unsafe {
                    let plen = cstr(&ow_page_title).len();
                    if plen < OW_URL_MAX - 1 {
                        ow_page_title[plen] = html[i];
                        ow_page_title[plen + 1] = 0;
                    }
                }
            }
            i += 1;
            continue;
        }

        /* ── pre ── */
        if st.in_pre {
            if html[i] == b'\n' {
                st.flush_buf();
                i += 1;
                continue;
            }
            if html[i] == b'\t' {
                /* Expand tab to next 8-column boundary */
                let mut spaces = 8 - (st.col % 8);
                while spaces > 0 && st.col < OW_TXT_COLS - 1 {
                    st.buf[st.col] = b' ';
                    st.col += 1;
                    spaces -= 1;
                }
                i += 1;
                continue;
            }
            if html[i] >= b' ' {
                if st.col < OW_TXT_COLS - 1 {
                    st.buf[st.col] = html[i];
                    st.col += 1;
                }
                i += 1;
                continue;
            }
            i += 1;
            continue;
        }

        if html[i] == b'\r' {
            i += 1;
            continue;
        }
        if html[i] == b'\n' || html[i] == b'\t' {
            if st.in_row {
                if st.col < OW_TXT_COLS - 1 {
                    st.buf[st.col] = b' ';
                    st.col += 1;
                }
                i += 1;
                continue;
            }
            if html[i] == b'\t' {
                if st.col < OW_TXT_COLS - 1 {
                    st.buf[st.col] = b' ';
                    st.col += 1;
                }
                if st.col < OW_TXT_COLS - 1 {
                    st.buf[st.col] = b' ';
                    st.col += 1;
                }
                i += 1;
                continue;
            }
            let para = i > 0 && html[i - 1] == b'\n';
            if st.col > 0 || para {
                st.flush_buf();
            }
            i += 1;
            continue;
        }

        if html[i] >= b' ' {
            if html[i] == b' ' {
                while i + 1 < html.len() && html[i + 1] == b' ' {
                    i += 1;
                }
            }
            if st.col >= OW_TXT_COLS - 1 {
                let mut brk: i32 = -1;
                let mut k = st.col as i32 - 1;
                while k >= 0 {
                    if st.buf[k as usize] == b' ' {
                        brk = k;
                        break;
                    }
                    k -= 1;
                }
                if brk > 0 {
                    let saved = st.col;
                    st.col = brk as usize;
                    st.flush_buf();
                    brk = st.col as i32; /* flush resets col to 0 */
                    let mut ni = 0usize;
                    let mut k = (brk + 1) as usize;
                    while k < saved {
                        st.buf[ni] = st.buf[k];
                        ni += 1;
                        k += 1;
                    }
                    st.col = ni;
                } else {
                    st.flush_buf();
                    st.col = 0;
                }
            }
            if st.in_a && st.cur_link >= 0 && (st.cur_link as usize) < OW_MAX_LINKS {
                unsafe {
                    if ow_links[st.cur_link as usize].line < 0 {
                        ow_links[st.cur_link as usize].line = ow_txt_lines;
                        ow_links[st.cur_link as usize].sc = st.col as c_int;
                    }
                }
            }
            if st.in_bold {
                st.line_bold = true;
            }
            if st.in_italic {
                st.line_italic = true;
            }
            if st.in_code {
                st.line_code = true;
            }
            st.buf[st.col] = html[i];
            st.col += 1;
            if st.in_a && st.cur_link >= 0 && (st.cur_link as usize) < OW_MAX_LINKS {
                unsafe {
                    ow_links[st.cur_link as usize].ec = st.col as c_int;
                }
            }
        }
        i += 1;
    }

    if st.col > 0 {
        st.flush_buf();
    }
    unsafe {
        if st.cur_form >= 0 && (st.cur_form as usize) < OW_MAX_FORMS {
            let fi = st.cur_form as usize;
            let fs = ow_forms[fi].field_start as c_int;
            ow_forms[fi].field_count = (ow_field_cnt - fs) as u8;
        }
        ow_need_render = 0;
    }
}

/// Write `mark` bytes into the image line at *ai (bounded).
fn st_img_mark(line: &mut [u8], ai: &mut usize, mark: &[u8]) {
    for &b in mark {
        if *ai < OW_TXT_COLS - 1 {
            line[*ai] = b;
            *ai += 1;
        }
    }
}
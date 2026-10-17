#![no_std]
#![no_main]

use core::ffi::{c_char, c_int};
use core::ptr;

mod ow_form;
mod ow_render;

/* ── Panic reporting ─────────────────────────────────────────────────────
 * The panic handler used to be a bare `loop {}`. With panic = "abort" that is
 * indistinguishable from a hang, a blocked fetch or a wedged scheduler — a
 * silent spin that takes the whole system with it, and gives no clue which.
 * It now reports the panic location on COM1 before spinning.
 *
 * Deliberately no `extern` declarations, so the crate stays linkable from
 * every context that pulls in libow_http.a (kernel, Qt panels). */
const COM1: u16 = 0x3F8;

/* COM1 is a *port*, not a memory-mapped device: the kernel's serial driver
 * reaches it with in/out instructions. Dereferencing 0x3F8/0x3FD instead is a
 * page fault on an unmapped page (CR2=0x3fd), so these must be real port I/O. */
#[inline(always)]
unsafe fn dbg_outb(port: u16, val: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") val,
                     options(nomem, nostack, preserves_flags));
}

#[inline(always)]
unsafe fn dbg_inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!("in al, dx", out("al") val, in("dx") port,
                     options(nomem, nostack, preserves_flags));
    val
}

fn dbg_putc(c: u8) {
    unsafe {
        let mut spins = 0u32;
        while dbg_inb(COM1 + 5) & 0x20 == 0 && spins < 100_000 {
            spins += 1;
        }
        dbg_outb(COM1, c);
    }
}

fn dbg_puts(s: &str) {
    for b in s.as_bytes() {
        dbg_putc(*b);
    }
}

fn dbg_putu(mut v: u32) {
    let mut tmp = [0u8; 10];
    let mut n = 0;
    if v == 0 {
        dbg_putc(b'0');
        return;
    }
    while v > 0 {
        tmp[n] = b'0' + (v % 10) as u8;
        v /= 10;
        n += 1;
    }
    while n > 0 {
        n -= 1;
        dbg_putc(tmp[n]);
    }
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    dbg_puts("\nOWPANIC: ");
    match info.location() {
        Some(loc) => {
            dbg_puts(loc.file());
            dbg_puts(":");
            dbg_putu(loc.line());
        }
        None => dbg_puts("<no location>"),
    }
    dbg_puts("\n");
    loop {}
}

const OW_URL_MAX: usize = 512;
const OW_CONTENT_MAX: usize = 65535;
const OW_MAX_TABS: usize = 12;
const OW_STATUS_MAX: usize = 80;

/* Maximum redirect chain depth — prevents infinite loops. */
const OW_MAX_REDIRECTS: usize = 8;

/* Per-tab navigation history (the WebView-style back/forward stacks). Every
 * OpenWeb frontend (Qt panel, tty `openweb`, Zircon browser) shares this. */
const OW_HIST_MAX: usize = 32;      /* max entries kept per tab */
const OW_HIST_URL_MAX: usize = 256; /* max stored history URL length */

#[repr(C)]
#[derive(Copy, Clone)]
pub struct OpenwebTab {
    pub url: [u8; OW_URL_MAX],
    pub content: [u8; OW_CONTENT_MAX],
    pub content_len: c_int,
    pub loading: c_int,
    pub scroll: c_int,
    pub status: [u8; OW_STATUS_MAX],
    pub error: c_int,
    pub can_go_back: c_int,
    pub can_go_forward: c_int,
    _redirect_depth: c_int,
}

impl OpenwebTab {
    const fn new() -> Self {
        Self {
            url: [0; OW_URL_MAX],
            content: [0; OW_CONTENT_MAX],
            content_len: 0,
            loading: 0,
            scroll: 0,
            status: [0; OW_STATUS_MAX],
            error: 0,
            can_go_back: 0,
            can_go_forward: 0,
            _redirect_depth: 0,
        }
    }
}

static mut TABS: [OpenwebTab; OW_MAX_TABS] = [
    OpenwebTab::new(), OpenwebTab::new(), OpenwebTab::new(), OpenwebTab::new(),
    OpenwebTab::new(), OpenwebTab::new(), OpenwebTab::new(), OpenwebTab::new(),
    OpenwebTab::new(), OpenwebTab::new(), OpenwebTab::new(), OpenwebTab::new(),
];
static mut TAB_COUNT: c_int = 0;
static mut TAB_ACTIVE: c_int = 0;
static mut OW_LOAD_PROGRESS: c_int = 0;

/* Per-tab history: ring of URLs plus the current position per tab. Indexed by
 * tab index and shifted together with TABS in ow_tab_close(). */
static mut HIST_URL: [[[u8; OW_HIST_URL_MAX]; OW_HIST_MAX]; OW_MAX_TABS] =
    [[[0; OW_HIST_URL_MAX]; OW_HIST_MAX]; OW_MAX_TABS];
static mut HIST_LEN: [c_int; OW_MAX_TABS] = [0; OW_MAX_TABS];
static mut HIST_POS: [c_int; OW_MAX_TABS] = [-1; OW_MAX_TABS];

/* Best-effort stop flag for an in-flight synchronous fetch. Fetches run to
 * completion in one call, so the flag is only polled between redirect/retry
 * iterations; it is cleared at the start of every navigation. */
static mut OW_STOP_REQUESTED: c_int = 0;

/* ── LRU page cache (final URL -> body) ── */
const OW_CACHE_ENTRIES: usize = 8;
static mut CACHE_URL: [[u8; OW_URL_MAX]; OW_CACHE_ENTRIES] = [[0; OW_URL_MAX]; OW_CACHE_ENTRIES];
static mut CACHE_BODY: [[u8; OW_CONTENT_MAX]; OW_CACHE_ENTRIES] = [[0; OW_CONTENT_MAX]; OW_CACHE_ENTRIES];
static mut CACHE_LEN: [c_int; OW_CACHE_ENTRIES] = [0; OW_CACHE_ENTRIES];
static mut CACHE_ORDER: [c_int; OW_CACHE_ENTRIES] = [0; OW_CACHE_ENTRIES];
static mut CACHE_CLOCK: c_int = 0;

fn cache_lookup(url: &[u8]) -> isize {
    /* Callers pass whole fixed-size buffers (`&t.url`, `&final_url`), so the
     * slice length is OW_URL_MAX, not the URL length — the NUL-terminated URL
     * is carried by the bytes, not by the slice bound. Indexing
     * CACHE_URL[i][url.len()] with len == OW_URL_MAX is out of bounds, so a
     * buffer that long can never hold a terminable URL and never matches.
     * The old `CACHE_URL[i][..].len() >= url.len()` guard was a tautology
     * (512 >= 512) and let that index through. */
    if url.is_empty() || url.len() >= OW_URL_MAX { return -1; }
    unsafe {
        for i in 0..OW_CACHE_ENTRIES {
            if CACHE_LEN[i] > 0
                && CACHE_URL[i][..url.len()] == *url
                && CACHE_URL[i][url.len()] == 0 {
                CACHE_CLOCK = CACHE_CLOCK.wrapping_add(1);
                CACHE_ORDER[i] = CACHE_CLOCK;
                return i as isize;
            }
        }
    }
    -1
}

fn cache_store(url: &[u8], body: &[u8]) {
    /* See cache_lookup: a slice as long as the destination buffer cannot be a
     * terminable URL, and the CACHE_URL[i][url.len()] probe below would be out
     * of bounds. */
    if url.is_empty() || url.len() >= OW_URL_MAX || body.is_empty()
        || body.len() > OW_CONTENT_MAX {
        return;
    }
    unsafe {
        let mut tgt: isize = -1;
        let mut victim: usize = 0;
        for i in 0..OW_CACHE_ENTRIES {
            if CACHE_LEN[i] > 0
                && CACHE_URL[i][..url.len()] == *url
                && CACHE_URL[i][url.len()] == 0 {
                tgt = i as isize;
                break;
            }
            if CACHE_ORDER[i] < CACHE_ORDER[victim] { victim = i; }
        }
        let e = if tgt >= 0 { tgt as usize } else { victim };
        let elen = core::cmp::min(url.len(), OW_URL_MAX - 1);
        CACHE_URL[e][..elen].copy_from_slice(&url[..elen]);
        CACHE_URL[e][elen] = 0;
        CACHE_BODY[e][..body.len()].copy_from_slice(body);
        CACHE_LEN[e] = body.len() as c_int;
        CACHE_CLOCK = CACHE_CLOCK.wrapping_add(1);
        CACHE_ORDER[e] = CACHE_CLOCK;
    }
}

extern "C" {
    fn sched_sleep_ms(ms: u64);
    fn http_get(host: *const c_char, port: u16, path: *const c_char, buf: *mut u8, max_len: u16) -> c_int;
    fn https_get(host: *const c_char, port: u16, path: *const c_char, buf: *mut u8, max_len: u16) -> c_int;
    fn http_post(host: *const c_char, port: u16, path: *const c_char,
                 body: *const u8, body_len: u16, buf: *mut u8, max_len: u16) -> c_int;
    fn fs_read(path: *const c_char, buf: *mut c_char, max: c_int) -> c_int;
}

// ── Utility helpers ──

fn strlen_u8(s: &[u8]) -> usize {
    s.iter().position(|&b| b == 0).unwrap_or(s.len())
}

fn cstrlen(s: *const c_char) -> usize {
    let mut len = 0;
    unsafe {
        while *s.add(len) != 0 { len += 1; }
    }
    len
}

fn copy_to_buf(dst: &mut [u8], src: &[u8]) {
    let copy_len = if src.len() < dst.len() { src.len() } else { dst.len() - 1 };
    dst[..copy_len].copy_from_slice(&src[..copy_len]);
    dst[copy_len] = 0;
}

fn buf_as_str(buf: &[u8]) -> &str {
    let len = strlen_u8(buf);
    core::str::from_utf8(&buf[..len]).unwrap_or("")
}

fn set_status(tab: &mut OpenwebTab, msg: &[u8]) {
    copy_to_buf(&mut tab.status, msg);
}

/// Build an error/status message by concatenating `prefix` + `suffix` into `out`.
fn build_msg(out: &mut [u8], prefix: &[u8], suffix: &[u8]) {
    let mut i = 0;
    for &b in prefix {
        if b == 0 || i >= out.len() - 1 { break; }
        out[i] = b;
        i += 1;
    }
    for &b in suffix {
        if i >= out.len() - 1 { break; }
        out[i] = b;
        i += 1;
    }
    out[i] = 0;
}

/// Format a u32 as decimal ASCII into `out`. Returns number of digits written.
fn format_u32(val: u32, out: &mut [u8]) -> usize {
    if val == 0 {
        if !out.is_empty() { out[0] = b'0'; }
        return 1;
    }
    let mut tmp = val;
    let mut digits = [0u8; 12];
    let mut di = 0;
    while tmp > 0 && di < 12 {
        digits[di] = b'0' + (tmp % 10) as u8;
        tmp /= 10;
        di += 1;
    }
    digits[..di].reverse();
    let n = if di < out.len() { di } else { out.len() };
    out[..n].copy_from_slice(&digits[..n]);
    n
}

// ── History (per-tab back/forward stacks) ──

/// Refresh `can_go_back` / `can_go_forward` for tab `i` from its history.
fn history_flags(i: usize) {
    unsafe {
        let len = HIST_LEN[i] as usize;
        let pos = if len == 0 { 0 } else { HIST_POS[i] as usize };
        let t = &mut *TABS.as_mut_ptr().add(i);
        t.can_go_back = if len > 0 && pos > 0 { 1 } else { 0 };
        t.can_go_forward = if len > 0 && pos + 1 < len { 1 } else { 0 };
    }
}

/// True when history entry `(i,pos)` holds exactly `url` (NUL-terminated).
fn history_entry_eq(i: usize, pos: usize, url: &[u8]) -> bool {
    let ulen = strlen_u8(url);
    /* Entries are truncated to OW_HIST_URL_MAX-1 bytes on store; anything
     * longer can never match (guards the `entry[ulen]` read against OOB). */
    if ulen == 0 || ulen >= OW_HIST_URL_MAX { return false; }
    unsafe {
        let entry = &HIST_URL[i][pos];
        entry[..ulen].eq(&url[..ulen]) && entry[ulen] == 0
    }
}

/// Record a navigation for tab `i`. Truncates any forward tail, appends the
/// URL, moves the current position to it, and refreshes the can_go flags.
/// Re-appending the exact current URL (e.g. a reload) is a no-op.
fn history_append(i: usize, url: &[u8]) {
    let ulen = strlen_u8(url);
    if ulen == 0 { history_flags(i); return; }
    unsafe {
        let len = HIST_LEN[i] as usize;
        let pos = if len == 0 { 0 } else { HIST_POS[i] as usize };
        if len > 0 && pos < len && history_entry_eq(i, pos, url) {
            history_flags(i);
            return;
        }
        /* Drop the forward tail (entries after the current position). */
        let mut write_pos = if len == 0 { 0 } else { pos + 1 };
        if write_pos >= OW_HIST_MAX {
            /* Ring full at the tail: drop the oldest entry and slide left. */
            for k in 1..OW_HIST_MAX {
                HIST_URL[i][k - 1].copy_from_slice(&HIST_URL[i][k]);
            }
            write_pos = OW_HIST_MAX - 1;
        }
        let n = core::cmp::min(ulen, OW_HIST_URL_MAX - 1);
        HIST_URL[i][write_pos][..n].copy_from_slice(&url[..n]);
        HIST_URL[i][write_pos][n] = 0;
        HIST_LEN[i] = (write_pos + 1) as c_int;
        HIST_POS[i] = write_pos as c_int;
        history_flags(i);
    }
}

// ── URL classification ──

fn is_unreserved(byte: u8) -> bool {
    (byte >= b'a' && byte <= b'z')
        || (byte >= b'A' && byte <= b'Z')
        || (byte >= b'0' && byte <= b'9')
        || byte == b'-' || byte == b'.' || byte == b'_' || byte == b'~'
}

fn has_known_scheme(url: &[u8]) -> bool {
    let s = buf_as_str(url);
    s.starts_with("http://") || s.starts_with("https://")
        || s.starts_with("file://") || s.starts_with("data://")
}

fn is_likely_url(input: &[u8]) -> bool {
    let s = buf_as_str(input);
    if s.is_empty() { return false; }
    if s.bytes().any(|b| b.is_ascii_whitespace() || b.is_ascii_control()) {
        return false;
    }
    if has_known_scheme(input) { return true; }
    if s.starts_with("localhost") { return true; }
    // IP/domain literal: a dotted string is a URL, even with an explicit
    // ":port" and/or path ("10.0.2.2:8095/docs.html"). Bare words without
    // dots fall through to the search-engine branch.
    s.find('.').is_some()
}

fn ensure_scheme(url: &mut [u8]) {
    if url[0] == 0 { return; }
    if has_known_scheme(url) { return; }
    let len = strlen_u8(url);
    if len + 7 >= url.len() - 1 { return; }
    unsafe {
        ptr::copy(url.as_ptr(), url.as_mut_ptr().add(7), len + 1);
        ptr::copy_nonoverlapping(b"http://".as_ptr(), url.as_mut_ptr(), 7);
    }
}

// ── URL parsing ──

fn parse_host_path(url: &[u8], host: &mut [u8], path: &mut [u8]) {
    let s = buf_as_str(url);
    let rest = if let Some(pos) = s.find("://") {
        &s[pos + 3..]
    } else {
        s
    };
    let slash_pos = rest.find('/');
    let mut hi = 0;
    match slash_pos {
        Some(pos) => {
            let h = &rest[..pos];
            for &b in h.as_bytes() {
                if hi >= host.len() - 1 { break; }
                host[hi] = b;
                hi += 1;
            }
            host[hi] = 0;
            let p = &rest[pos..];
            let mut pi = 0;
            for &b in p.as_bytes() {
                if pi >= path.len() - 1 { break; }
                path[pi] = b;
                pi += 1;
            }
            path[pi] = 0;
        }
        None => {
            for &b in rest.as_bytes() {
                if hi >= host.len() - 1 { break; }
                host[hi] = b;
                hi += 1;
            }
            host[hi] = 0;
            path[0] = b'/';
            path[1] = 0;
        }
    }
}

fn build_search_url(query: &str, out: &mut [u8]) {
    let prefix = b"http://www.google.com/search?q=";
    let mut i = 0;
    while i < prefix.len() && i + 1 < out.len() {
        out[i] = prefix[i];
        i += 1;
    }
    for &byte in query.as_bytes() {
        if is_unreserved(byte) {
            if i + 1 >= out.len() { break; }
            out[i] = byte;
            i += 1;
        } else {
            if i + 3 >= out.len() { break; }
            out[i] = b'%';
            out[i + 1] = b"0123456789ABCDEF"[(byte >> 4) as usize];
            out[i + 2] = b"0123456789ABCDEF"[(byte & 0x0F) as usize];
            i += 3;
        }
    }
    out[i] = 0;
}

// ── HTTP layer ──

fn http_fetch(url: &[u8], host: &[u8], path: &[u8], body: Option<&[u8]>, buf: &mut [u8]) -> c_int {
    // TLS when the URL scheme is https:// — the kernel https_get() performs the
    // full mbedtls handshake + HTTP/1.1 exchange (accept: * / *, conn: close).
    let https = buf_as_str(url).starts_with("https://");
    // The authority may carry an explicit ":port" ("10.0.2.2:8095").
    let host_str = buf_as_str(host);
    let mut host_no_port = [0u8; 128];
    let mut port: u16 = if https { 443 } else { 80 };
    if let Some(ci) = host_str.find(':') {
        let port_str = &host_str[ci + 1..];
        let mut p: u32 = 0;
        let mut valid = !port_str.is_empty();
        for &b in port_str.as_bytes() {
            if b < b'0' || b > b'9' { valid = false; break; }
            p = p * 10 + (b - b'0') as u32;
        }
        if valid && p <= 65535 { port = p as u16; }
        if ci < host_no_port.len() {
            host_no_port[..ci].copy_from_slice(&host_str.as_bytes()[..ci]);
        }
    } else if host_str.len() < host_no_port.len() {
        host_no_port[..host_str.len()].copy_from_slice(host_str.as_bytes());
    }
    let host_c = host_no_port.as_ptr() as *const c_char;
    let path_c = path.as_ptr() as *const c_char;
    unsafe {
        match body {
            Some(b) => {
                let bl = core::cmp::min(b.len(), u16::MAX as usize);
                http_post(host_c, port, path_c, b.as_ptr(), bl as u16,
                          buf.as_mut_ptr(), buf.len() as u16)
            }
            None => {
                if https {
                    https_get(host_c, port, path_c, buf.as_mut_ptr(), buf.len() as u16)
                } else {
                    http_get(host_c, port, path_c, buf.as_mut_ptr(), buf.len() as u16)
                }
            }
        }
    }
}

/// Parse the HTTP status code from the response header. Returns 0 on failure.
fn parse_status_code(data: &[u8]) -> c_int {
    if data.len() < 12 || &data[..5] != b"HTTP/" { return 0; }
    // "HTTP/1.x NNN"
    if data.len() > 9 && data[9] >= b'0' && data[9] <= b'9' {
        let mut code: c_int = 0;
        let mut ci = 9;
        let end = if data.len() < 12 { data.len() } else { 12 };
        while ci < end && data[ci] >= b'0' && data[ci] <= b'9' {
            code = code.wrapping_mul(10).wrapping_add((data[ci] - b'0') as c_int);
            ci += 1;
        }
        return code;
    }
    0
}

/// Find the body (after `\r\n\r\n`) in an HTTP response. Returns the body slice.
fn find_body<'a>(data: &'a [u8]) -> &'a [u8] {
    for i in 0..data.len().saturating_sub(3) {
        if data[i] == b'\r' && data[i+1] == b'\n' && data[i+2] == b'\r' && data[i+3] == b'\n' {
            return &data[i + 4..];
        }
    }
    data
}

/// Extract a header value from response body text. Returns the trimmed value.
fn extract_header<'a>(body: &'a [u8], name: &str) -> Option<&'a str> {
    let body_str = core::str::from_utf8(body).unwrap_or("");
    let pos = body_str.find(name)?;
    let after = &body_str[pos..];
    let colon = after.find(':').unwrap_or(0);
    let val = after[colon + 1..].trim();
    if val.is_empty() { None } else { Some(val) }
}

/// Copy `src` into `dst` (NUL terminated, truncated).
fn copy_bytes_into(dst: &mut [u8], src: &[u8]) {
    let n = if src.len() < dst.len() { src.len() } else { dst.len() - 1 };
    dst[..n].copy_from_slice(&src[..n]);
    dst[n] = 0;
}

/// Append `src` at the end of the NUL-terminated string in `dst`.
fn append_bytes_into(dst: &mut [u8], src: &[u8]) {
    let cur = strlen_u8(dst);
    if cur >= dst.len() - 1 { return; }
    let space = dst.len() - 1 - cur;
    let n = if src.len() < space { src.len() } else { space };
    dst[cur..cur + n].copy_from_slice(&src[..n]);
    dst[cur + n] = 0;
}

/// Resolve a (possibly relative / scheme-relative) Location against the tab's
/// current URL. Returns true and writes an absolute URL to `out` on success.
fn resolve_location(cur_url: &[u8], loc: Option<&str>, out: &mut [u8]) -> bool {
    let Some(loc) = loc else { return false };
    let lb = loc.as_bytes();
    if lb.is_empty() { return false; }

    /* Absolute (http(s)://...) or protocol-relative (//host/path). */
    if has_known_scheme(lb) || lb.starts_with(b"//") {
        copy_bytes_into(out, lb);
        return true;
    }

    let cur = buf_as_str(cur_url);
    let Some(se) = cur.find("://") else {
        /* No usable base — fall back to the bare location. */
        copy_bytes_into(out, lb);
        return true;
    };

    /* End of the authority (host[:port]) = first '/' after the scheme. */
    let scheme_end = se + 3;
    let authority_end = match cur[scheme_end..].find('/') {
        Some(i) => scheme_end + i,
        None => cur.len(),
    };

    if lb.first() == Some(&b'/') {
        /* Same host, absolute path: scheme://authority + /path */
        copy_bytes_into(out, cur[..authority_end].as_bytes());
        append_bytes_into(out, lb);
        return true;
    }

    /* Relative path: directory of the current URL + loc. */
    let base_len = {
        let mut cut = authority_end;
        for i in (scheme_end..authority_end).rev() {
            if cur[..authority_end].as_bytes()[i] == b'/' {
                cut = i + 1;
                break;
            }
        }
        cut
    };
    let mut base = [0u8; OW_URL_MAX];
    copy_bytes_into(&mut base, cur[..base_len].as_bytes());
    copy_bytes_into(out, &base[..strlen_u8(&base)]);
    append_bytes_into(out, lb);
    true
}

/// Follow a redirect chain iteratively. `classify_and_fetch` is the single
/// entry used by every C export; redirects loop back into the fetch with an
/// explicit counter instead of recursing, so a hostile redirect chain cannot
/// grow the kernel stack (each frame holds ~64 KiB of response buffer).
fn classify_and_fetch(tab_idx: c_int, raw_input: &[u8]) {
    classify_and_fetch_inner(tab_idx, raw_input, None, false);
}

fn classify_and_fetch_inner(tab_idx: c_int, raw_input: &[u8], body: Option<&[u8]>, refresh: bool) {
    unsafe {
        OW_STOP_REQUESTED = 0;
        let t = &mut *TABS.as_mut_ptr().add(tab_idx as usize);

        let mut final_url = [0u8; OW_URL_MAX];

        if is_likely_url(raw_input) {
            copy_to_buf(&mut t.url, raw_input);
            ensure_scheme(&mut t.url);
            copy_to_buf(&mut final_url, &t.url);
        } else {
            let query = buf_as_str(raw_input);
            if query.is_empty() {
                set_status(t, b"Empty query\0");
                t.loading = 0;
                return;
            }
            build_search_url(query, &mut final_url);
            copy_to_buf(&mut t.url, &final_url);
        }

        if !refresh && body.is_none() {
            /* Pass the NUL-terminated extent (URL + terminator), not the whole
             * buffer: the cache keys on exactly these bytes, so a zero-padded
             * 512-byte slice would never match a stored key. */
            let key_len = core::cmp::min(strlen_u8(&final_url) + 1, OW_URL_MAX);
            let hit = cache_lookup(&final_url[..key_len]);
            if hit >= 0 {
                let e = hit as usize;
                let n = CACHE_LEN[e] as usize;
                t.content[..n].copy_from_slice(&CACHE_BODY[e][..n]);
                t.content_len = n as c_int;
                set_status(t, b"Loaded (cached)\0");
                t.loading = 0;
                t.error = 0;
                OW_LOAD_PROGRESS = 100;
                t.scroll = 0;
                return;
            }
        }

        /* ── data: URI inline content ── */
        let url_str = buf_as_str(&final_url);
        if url_str.starts_with("data:") || url_str.starts_with("data://") {
            let data_start = if url_str.starts_with("data://") { 7 } else { 5 };
            let data = &final_url[data_start..];
            let data_s = buf_as_str(data);
            /* Find the comma separating the optional MIME from the content */
            if let Some(comma_pos) = data_s.find(',') {
                let content = &data_s[comma_pos + 1..];
                let cl = content.len();
                let copy_len = if cl < OW_CONTENT_MAX as usize { cl } else { OW_CONTENT_MAX as usize - 1 };
                let content_bytes = content.as_bytes();
                t.content[..copy_len].copy_from_slice(&content_bytes[..copy_len]);
                t.content[copy_len] = 0;
                t.content_len = copy_len as c_int;
                set_status(t, b"Loaded (data:)\0");
                t.loading = 0;
                t.error = 0;
                OW_LOAD_PROGRESS = 100;
                t.scroll = 0;
                cache_store(&final_url[..core::cmp::min(strlen_u8(&final_url) + 1, OW_URL_MAX)],
                            &t.content[..copy_len]);
                return;
            }
        }

        /* ── file: URI local filesystem ── */
        if url_str.starts_with("file://") {
            let path_start = 7; /* skip "file://" */
            let path_s = buf_as_str(&final_url[path_start..]);
            /* Convert to NUL-terminated for C fs_read */
            let mut cpath = [0u8; OW_URL_MAX];
            let plen = core::cmp::min(path_s.len(), OW_URL_MAX - 1);
            cpath[..plen].copy_from_slice(&path_s.as_bytes()[..plen]);
            cpath[plen] = 0;
            unsafe {
                let n = fs_read(
                    cpath.as_ptr() as *const c_char,
                    t.content.as_mut_ptr() as *mut c_char,
                    OW_CONTENT_MAX as c_int - 1,
                );
                if n > 0 {
                    t.content_len = n;
                    t.content[n as usize] = 0;
                    set_status(t, b"Loaded (file:)\0");
                    t.error = 0;
                } else {
                    t.content_len = 0;
                    t.error = 5;
                    set_status(t, b"File not found\0");
                }
            }
            t.loading = 0;
            OW_LOAD_PROGRESS = 100;
            t.scroll = 0;
            return;
        }

        t._redirect_depth = 0;
        let mut redirects: c_int = 0;

        loop {
            if OW_STOP_REQUESTED != 0 {
                set_status(t, b"Stopped\0");
                t.loading = 0;
                t.error = 0;
                t.content_len = 0;
                OW_LOAD_PROGRESS = 0;
                break;
            }
            set_status(t, b"Loading...\0");
            t.loading = 1;
            t.error = 0;
            t.content_len = 0;
            OW_LOAD_PROGRESS = 0;

            let mut host = [0u8; 128];
            let mut path = [0u8; 512];
            parse_host_path(&final_url, &mut host, &mut path);

            /* Build "Loading <host>..." status message. */
            let mut status_msg = [0u8; 80];
            let host_str = buf_as_str(&host);
            build_msg(&mut status_msg, b"Loading ", host_str.as_bytes());
            /* Append "..." — build_msg doesn't do multi-part, so manually: */
            let base_len = strlen_u8(&status_msg);
            if base_len + 3 < status_msg.len() {
                status_msg[base_len] = b'.';
                status_msg[base_len + 1] = b'.';
                status_msg[base_len + 2] = b'.';
                status_msg[base_len + 3] = 0;
            }
            set_status(t, &status_msg);

            let mut resp_buf = [0u8; OW_CONTENT_MAX];
            let mut n = http_fetch(&final_url, &host, &path, body, &mut resp_buf);
            if n < 0 {
                /* The stack only keeps one TCP connection at a time; a concurrent
                 * user (updater, pkg sync) can make our connect fail transiently.
                 * Retry once after a short pause before falling through to error. */
                sched_sleep_ms(150);
                n = http_fetch(&final_url, &host, &path, body, &mut resp_buf);
            }

            if n < 0 {
                t.error = 2;
                let mut err_msg = [0u8; 80];
                build_msg(&mut err_msg, b"IP needed: ", host_str.as_bytes());
                set_status(t, &err_msg);
                t.content_len = 0;
                break;
            } else if n == 0 {
                t.error = 5;
                let mut err_msg = [0u8; 80];
                build_msg(&mut err_msg, b"No data: ", host_str.as_bytes());
                set_status(t, &err_msg);
                t.content_len = 0;
                break;
            }

            let data = &resp_buf[..n as usize];
            let code = parse_status_code(data);

            if code >= 300 && code < 400 {
                /* Location lives in the response HEADERS (data), not the body. */
                redirects += 1;
                if redirects >= OW_MAX_REDIRECTS as c_int {
                    set_status(t, b"Too many redirects\0");
                    t.loading = 0;
                    t.error = 5;
                    break;
                }
                let mut redir = [0u8; OW_URL_MAX];
                if !resolve_location(&t.url, extract_header(data, "Location:"), &mut redir) {
                    set_status(t, b"Redirect without Location\0");
                    t.loading = 0;
                    t.error = 5;
                    break;
                }
                set_status(t, b"Redirect...\0");
                copy_to_buf(&mut final_url, &redir);
                copy_to_buf(&mut t.url, &redir);
                continue;
            }

            if code >= 400 {
                t.error = 5;
                let mut err_msg = [0u8; 80];
                let mut ei = 0;
                for &b in b"HTTP error " {
                    if ei >= err_msg.len() - 1 { break; }
                    err_msg[ei] = b;
                    ei += 1;
                }
                let code_str = [
                    b'0' + (code / 100) as u8,
                    b'0' + ((code / 10) % 10) as u8,
                    b'0' + (code % 10) as u8,
                ];
                for &b in &code_str {
                    if ei >= err_msg.len() - 1 { break; }
                    err_msg[ei] = b;
                    ei += 1;
                }
                err_msg[ei] = 0;
                set_status(t, &err_msg);
            }

            let body = find_body(data);
            let copy_len = if body.len() < OW_CONTENT_MAX { body.len() } else { OW_CONTENT_MAX };
            t.content[..copy_len].copy_from_slice(&body[..copy_len]);
            t.content_len = copy_len as c_int;

            if t.error == 0 {
                let mut ok_msg = [0u8; 80];
                let mut mi = 0;
                for &b in b"Loaded " {
                    if mi >= ok_msg.len() - 1 { break; }
                    ok_msg[mi] = b;
                    mi += 1;
                }
                let mut digits_buf = [0u8; 12];
                let ndigits = format_u32(body.len() as u32, &mut digits_buf);
                for i in 0..ndigits {
                    if mi >= ok_msg.len() - 1 { break; }
                    ok_msg[mi] = digits_buf[i];
                    mi += 1;
                }
                if mi < ok_msg.len() - 3 {
                    ok_msg[mi] = b' ';
                    ok_msg[mi+1] = 0xE2u8;
                    ok_msg[mi+2] = 0x82;
                    ok_msg[mi+3] = 0xA9;
                    mi += 4;
                }
                for &b in host_str.as_bytes() {
                    if mi >= ok_msg.len() - 1 { break; }
                    ok_msg[mi] = b;
                    mi += 1;
                }
                ok_msg[mi] = 0;
                set_status(t, &ok_msg);
            }
            break;
        }
        if body.is_none() && t.error == 0 && t.content_len > 0 {
            cache_store(&final_url[..core::cmp::min(strlen_u8(&final_url) + 1, OW_URL_MAX)],
                        &t.content[..t.content_len as usize]);
        }
        t.loading = 0;
        OW_LOAD_PROGRESS = 100;
        t.scroll = 0;
    }
}

fn tab_find_empty() -> c_int {
    unsafe {
        for i in 0..OW_MAX_TABS {
            if TABS[i].url[0] == 0 { return i as c_int; }
        }
    }
    -1
}

// ── C ABI exports ──

#[no_mangle]
pub extern "C" fn ow_get_tabs() -> *mut OpenwebTab {
    unsafe { TABS.as_mut_ptr() }
}

#[no_mangle]
pub extern "C" fn ow_get_tab_count() -> c_int {
    unsafe { TAB_COUNT }
}

#[no_mangle]
pub extern "C" fn ow_get_tab_active() -> c_int {
    unsafe { TAB_ACTIVE }
}

#[no_mangle]
pub extern "C" fn ow_set_tab_active(idx: c_int) {
    unsafe {
        if idx >= 0 && idx < TAB_COUNT {
            TAB_ACTIVE = idx;
        }
    }
}

#[no_mangle]
pub extern "C" fn ow_get_load_progress() -> c_int {
    unsafe { OW_LOAD_PROGRESS }
}

#[no_mangle]
pub extern "C" fn ow_tab_new(url: *const c_char) {
    unsafe {
        let idx = tab_find_empty();
        if idx < 0 { return; }
        let t = &mut *TABS.as_mut_ptr().add(idx as usize);
        ptr::write_bytes(t as *mut OpenwebTab, 0, 1);
        if !url.is_null() && *url != 0 {
            c_copy_to_buf(&mut t.url, url);
        } else {
            /* Default to about:blank - user can navigate elsewhere */
            let blank = b"about:blank\0";
            c_copy_to_buf(&mut t.url, blank.as_ptr() as *const c_char);
        }
        set_status(t, b"New tab\0");
        if idx >= TAB_COUNT { TAB_COUNT = idx + 1; }
        TAB_ACTIVE = idx;
        /* Record the initial URL (but not about: pages) so history works from
         * the moment a tab is opened. */
        let mut starts_about = t.url[0] == 0;
        if !starts_about {
            starts_about = true;
            for (k, &c) in b"about:".iter().enumerate() {
                if t.url[k] != c { starts_about = false; break; }
            }
        }
        if !starts_about {
            history_append(idx as usize, &t.url);
        }
    }
}

#[no_mangle]
pub extern "C" fn ow_tab_close(idx: c_int) {
    unsafe {
        let count = TAB_COUNT;
        if idx < 0 || idx >= count { return; }
        let was_active = TAB_ACTIVE == idx;
        let mut others_used = false;
        for i in 0..count {
            if i == idx { continue; }
            if TABS[i as usize].url[0] != 0 { others_used = true; break; }
        }
        for i in idx as usize..(count as usize - 1) {
            TABS[i] = TABS[i + 1];
            /* Shift the per-tab history together with the tab array so the
             * back/forward stacks stay attached to the right tab. */
            HIST_LEN[i] = HIST_LEN[i + 1];
            HIST_POS[i] = HIST_POS[i + 1];
            for k in 0..OW_HIST_MAX {
                HIST_URL[i][k].copy_from_slice(&HIST_URL[i + 1][k]);
            }
        }
        let last = count as usize - 1;
        /* write_bytes takes a count *of elements*: passing size_of::<OpenwebTab>()
         * here would zero sizeof^2 bytes (0x104dddd90) past TABS and fault. */
        ptr::write_bytes(TABS.as_mut_ptr().add(last), 0, 1);
        HIST_LEN[last] = 0;
        HIST_POS[last] = -1;
        TAB_COUNT = count - 1;
        if was_active {
            if others_used && TAB_COUNT > 0 {
                TAB_ACTIVE = if idx < TAB_COUNT { idx } else { TAB_COUNT - 1 };
            } else {
                TAB_ACTIVE = 0;
            }
        } else if TAB_ACTIVE > idx {
            TAB_ACTIVE -= 1;
        }
        if TAB_COUNT <= 0 || !others_used {
            let blank = b"about:blank\0";
            ow_tab_new(blank.as_ptr() as *const c_char);
        }
    }
}

#[no_mangle]
pub extern "C" fn ow_tab_used_count() -> c_int {
    unsafe {
        let mut n: c_int = 0;
        for i in 0..OW_MAX_TABS {
            if TABS[i].url[0] != 0 { n += 1; }
        }
        n
    }
}

#[no_mangle]
pub extern "C" fn ow_navigate(url: *const c_char) {
    if url.is_null() { return; }
    unsafe {
        let idx = TAB_ACTIVE;
        let t = &mut *TABS.as_mut_ptr().add(idx as usize);
        c_copy_to_buf(&mut t.url, url);
        classify_and_fetch(idx, &t.url);
        /* t.url now holds the final (scheme-normalized / redirect-resolved)
         * URL; record it so back/forward have an entry for this navigation. */
        history_append(idx as usize, &t.url);
    }
}

/// Navigate the active tab with an HTTP POST body (form submission).
/// The kernel C layer only has a plain-HTTP POST client, so https targets
/// degrade to http_post on the resolved host:port (documented limitation).
#[no_mangle]
pub extern "C" fn ow_navigate_post(action: *const c_char, body: *const u8, body_len: c_int) {
    if action.is_null() { return; }
    unsafe {
        let idx = TAB_ACTIVE;
        let t = &mut *TABS.as_mut_ptr().add(idx as usize);
        c_copy_to_buf(&mut t.url, action);
        let body_slice: &[u8] = if !body.is_null() && body_len > 0 {
            core::slice::from_raw_parts(body, body_len as usize)
        } else {
            &[]
        };
        classify_and_fetch_inner(idx, &t.url, Some(body_slice), false);
        history_append(idx as usize, &t.url);
    }
}

/// Navigate the active tab ignoring the page cache (fresh reload).
#[no_mangle]
pub extern "C" fn ow_navigate_fresh(url: *const c_char) {
    if url.is_null() { return; }
    unsafe {
        let idx = TAB_ACTIVE;
        let t = &mut *TABS.as_mut_ptr().add(idx as usize);
        c_copy_to_buf(&mut t.url, url);
        classify_and_fetch_inner(idx, &t.url, None, true);
    }
}

#[no_mangle]
pub extern "C" fn ow_search(query: *const c_char) {
    if query.is_null() { return; }
    unsafe {
        let query_str = {
            let mut len = 0;
            while *query.add(len) != 0 { len += 1; }
            core::str::from_utf8_unchecked(core::slice::from_raw_parts(query as *const u8, len))
        };
        let mut search_url = [0u8; OW_URL_MAX];
        build_search_url(query_str, &mut search_url);
        let idx = TAB_ACTIVE;
        copy_to_buf(&mut TABS[idx as usize].url, &search_url);
        classify_and_fetch(idx, &search_url);
        history_append(idx as usize, &TABS[idx as usize].url);
    }
}

/// Move the active tab one step back in its history and reload that page.
#[no_mangle]
pub extern "C" fn ow_go_back() {
    unsafe {
        let idx = TAB_ACTIVE;
        if idx < 0 || idx >= TAB_COUNT { return; }
        let i = idx as usize;
        let len = HIST_LEN[i] as usize;
        if len == 0 { history_flags(i); return; }
        let pos = HIST_POS[i] as usize;
        if pos == 0 { history_flags(i); return; }
        let npos = pos - 1;
        HIST_POS[i] = npos as c_int;
        let t = &mut *TABS.as_mut_ptr().add(idx as usize);
        let entry = &HIST_URL[i][npos];
        let elen = strlen_u8(entry);
        t.url[..elen].copy_from_slice(&entry[..elen]);
        t.url[elen] = 0;
        /* Hand down URL + terminator, not the whole buffer. buf_as_str() runs
         * strlen over the slice and then UTF-8-validates it, so trailing stale
         * bytes past the NUL would silently turn the navigation into a search. */
        classify_and_fetch_inner(idx, &t.url[..elen + 1], None, false);
        history_flags(i);
    }
}

/// Move the active tab one step forward in its history and reload that page.
#[no_mangle]
pub extern "C" fn ow_go_forward() {
    unsafe {
        let idx = TAB_ACTIVE;
        if idx < 0 || idx >= TAB_COUNT { return; }
        let i = idx as usize;
        let len = HIST_LEN[i] as usize;
        if len == 0 { history_flags(i); return; }
        let pos = HIST_POS[i] as usize;
        if pos + 1 >= len { history_flags(i); return; }
        let npos = pos + 1;
        HIST_POS[i] = npos as c_int;
        let t = &mut *TABS.as_mut_ptr().add(idx as usize);
        let entry = &HIST_URL[i][npos];
        let elen = strlen_u8(entry);
        t.url[..elen].copy_from_slice(&entry[..elen]);
        t.url[elen] = 0;
        /* URL + terminator, not the whole buffer — see ow_go_back. */
        classify_and_fetch_inner(idx, &t.url[..elen + 1], None, false);
        history_flags(i);
    }
}

/// Best-effort cancel of an in-flight load. Fetches are synchronous, so the
/// request is honoured at the next redirect/retry boundary; an already-stopped
/// tab is reset to idle with a "Stopped" status.
#[no_mangle]
pub extern "C" fn ow_stop_loading() {
    unsafe {
        OW_STOP_REQUESTED = 1;
        let idx = TAB_ACTIVE;
        if idx >= 0 && idx < TAB_COUNT {
            let t = &mut *TABS.as_mut_ptr().add(idx as usize);
            if t.loading != 0 {
                t.loading = 0;
                t.error = 0;
                set_status(t, b"Stopped\0");
            }
            OW_LOAD_PROGRESS = 0;
        }
    }
}

fn c_copy_to_buf(dst: &mut [u8], src: *const c_char) {
    let len = cstrlen(src);
    let copy_len = if len < dst.len() { len } else { dst.len() - 1 };
    unsafe {
        ptr::copy_nonoverlapping(src, dst.as_mut_ptr() as *mut c_char, copy_len);
    }
    dst[copy_len] = 0;
}

#[path = "../../../../kernel/kernel/rust_ow/src/ow_render.rs"]
mod ow_render;

use std::ffi::{c_char, c_int, c_void};

const COLS: usize = 120;
const LINES: usize = 512;
const URL_MAX: usize = 512;
const MAX_LINKS: usize = 128;
const MAX_IMAGES: usize = 32;
const MAX_FORMS: usize = 8;
const MAX_FIELDS: usize = 40;
const MAX_SELECT_OPTS: usize = 16;
const SELECT_OPT_SZ: usize = 56;

#[repr(C)]
#[derive(Copy, Clone)]
struct OwLink {
    line: c_int,
    sc: c_int,
    ec: c_int,
    url: [u8; URL_MAX],
}

#[repr(C)]
#[derive(Copy, Clone)]
struct OwImage {
    url: [u8; URL_MAX],
    x: c_int,
    y: c_int,
    w: c_int,
    h: c_int,
    loaded: c_int,
    pixmap: *mut c_void,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct OwLineInfo {
    typ: u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct OwForm {
    action: [u8; URL_MAX],
    method: u8,
    field_start: u8,
    field_count: u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct OwFormField {
    typ: u8,
    form: c_int,
    name: [u8; URL_MAX],
    value: [u8; URL_MAX],
    line: c_int,
    col: c_int,
    width: c_int,
    checked: u8,
    opt_cnt: u8,
    opt_sel: u8,
    opts: [[u8; SELECT_OPT_SZ]; MAX_SELECT_OPTS],
}

#[no_mangle]
pub static mut ow_txt: [[u8; COLS]; LINES] = [[0; COLS]; LINES];
#[no_mangle]
pub static mut ow_txt_lines: c_int = 0;
#[no_mangle]
pub static mut ow_links: [OwLink; MAX_LINKS] = [OwLink {
    line: 0,
    sc: 0,
    ec: 0,
    url: [0; URL_MAX],
}; MAX_LINKS];
#[no_mangle]
pub static mut ow_link_cnt: c_int = 0;
#[no_mangle]
pub static mut ow_images: [OwImage; MAX_IMAGES] = [OwImage {
    url: [0; URL_MAX],
    x: 0,
    y: 0,
    w: 0,
    h: 0,
    loaded: 0,
    pixmap: std::ptr::null_mut(),
}; MAX_IMAGES];
#[no_mangle]
pub static mut ow_image_cnt: c_int = 0;
#[no_mangle]
pub static mut ow_need_render: c_int = 0;
#[no_mangle]
pub static mut ow_line_info: [OwLineInfo; LINES] = [OwLineInfo { typ: 0 }; LINES];
#[no_mangle]
pub static mut ow_line_img: [c_int; LINES] = [0; LINES];
#[no_mangle]
pub static mut ow_page_title: [u8; URL_MAX] = [0; URL_MAX];
#[no_mangle]
pub static mut ow_page_desc: [u8; URL_MAX] = [0; URL_MAX];
#[no_mangle]
pub static mut ow_forms: [OwForm; MAX_FORMS] = [OwForm {
    action: [0; URL_MAX],
    method: 0,
    field_start: 0,
    field_count: 0,
}; MAX_FORMS];
#[no_mangle]
pub static mut ow_form_cnt: c_int = 0;
#[no_mangle]
pub static mut ow_form_fields: [OwFormField; MAX_FIELDS] = [OwFormField {
    typ: 0,
    form: 0,
    name: [0; URL_MAX],
    value: [0; URL_MAX],
    line: 0,
    col: 0,
    width: 0,
    checked: 0,
    opt_cnt: 0,
    opt_sel: 0,
    opts: [[0; SELECT_OPT_SZ]; MAX_SELECT_OPTS],
}; MAX_FIELDS];
#[no_mangle]
pub static mut ow_field_cnt: c_int = 0;

#[no_mangle]
pub extern "C" fn ow_fv_restore(_f: *mut OwFormField, _fi: c_int) {}
#[no_mangle]
pub extern "C" fn ow_fv_store(_f: *const OwFormField, _fi: c_int) {}

fn render(html: &str) {
    ow_render::ow_render_rs(html.as_ptr() as *const c_char, html.len() as c_int);
}

fn lines() -> Vec<String> {
    let count = unsafe { ow_txt_lines } as usize;
    (0..count.min(LINES))
        .map(|i| {
            let row = unsafe { &ow_txt[i] };
            let n = row.iter().position(|&b| b == 0).unwrap_or(COLS);
            String::from_utf8_lossy(&row[..n]).into_owned()
        })
        .collect()
}

fn expect(condition: bool, message: &str) {
    if !condition {
        panic!("FAIL: {message}");
    }
    println!("ok: {message}");
}

fn main() {
    render(
        "<table><thead><tr><th>Head One</th><th>Head Two</th></tr></thead>\
         <tbody><tr><td>Alpha</td><td>Beta</td></tr></tbody></table>",
    );
    let table = lines();
    println!("table lines: {table:#?}");
    expect(
        table.iter().any(|l| l == "Head One") && table.iter().any(|l| l.starts_with(" | Head Two")),
        "table header cells keep their row state and separator prefix",
    );
    expect(
        table.iter().any(|l| l == "Alpha") && table.iter().any(|l| l.starts_with(" | Beta")),
        "table body cells keep their row state and separator prefix",
    );
    expect(
        table.iter().any(|l| l.len() > 20 && l.chars().all(|c| c == '-')),
        "table rows still emit a separator rule",
    );

    render("<p>&#65;&#x42;&#xD800;&#x1F600;</p>");
    let entities = lines();
    println!("entity lines: {entities:#?}");
    expect(
        entities.iter().any(|l| l.contains("AB")),
        "numeric entities decode to ASCII",
    );
    expect(
        entities.iter().any(|l| l.contains("\u{1F600}")),
        "astral numeric entities decode to UTF-8",
    );
    expect(
        !entities.iter().any(|l| l.as_bytes().windows(3).any(|w| w == [0xED, 0xA0, 0x80])),
        "surrogate numeric references do not produce invalid UTF-8",
    );

    let words = vec!["aaaaaaaaaa"; 14].join(" ");
    render(&format!("<p>{words}</p>"));
    let wrapped = lines();
    println!("wrapped lines: {wrapped:#?}");
    expect(
        wrapped.len() >= 2 && wrapped[1].starts_with("aaaaaaaaaa"),
        "word-wrap continuation keeps the first byte",
    );

    println!("ow_render fixtures: all checks passed");
}

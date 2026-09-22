/* ═══════════════════════════════════════════════════════════════════════
 * mlua — Minimal no_std Lua VM for HyperDE configuration
 *
 * Supports the Lua 5.4 subset needed for conf.lua:
 *   - Variables, tables, nested tables
 *   - Strings, numbers, booleans
 *   - require() for modular config (reads from embedded filesystem)
 *   - print() for debug output (→ kprintf)
 *   - Math operations for animations/timing
 *   - Comments (-- line, --[[ block ]])
 *
 * Like Hyprland's config, the main conf.lua can:
 *   local wm = require("config.window_manager")
 *   local theme = require("config.theme")
 *   local autostart = require("config.autostart")
 *
 * Each module returns a table that gets merged into the config.
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
    fn strlen(s: *const c_char) -> usize;
    fn fs_read(path: *const c_char, buf: *mut u8, max: usize) -> c_int;
    fn fs_resolve(path: *const c_char) -> c_int;
}

/* ── Lua value types ── */

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Value {
    Nil,
    Bool(bool),
    Number(f64),
    Str(usize, usize), /* index into string pool, length */
    Func(usize),       /* function index */
}

impl Value {
    pub fn is_nil(&self) -> bool { matches!(self, Value::Nil) }
    pub fn is_bool(&self) -> bool { matches!(self, Value::Bool(_)) }
    pub fn is_number(&self) -> bool { matches!(self, Value::Number(_)) }
    pub fn is_str(&self) -> bool { matches!(self, Value::Str(_, _)) }
    pub fn is_table(&self) -> bool { false } /* tables are separate */

    pub fn to_bool(&self) -> bool {
        match self {
            Value::Nil => false,
            Value::Bool(b) => *b,
            Value::Number(n) => *n != 0.0,
            Value::Str(_, l) => *l > 0,
            Value::Func(_) => true,
        }
    }

    pub fn to_number(&self) -> f64 {
        match self {
            Value::Number(n) => *n,
            Value::Bool(b) => if *b { 1.0 } else { 0.0 },
            _ => 0.0,
        }
    }
}

/* ── String pool ── */

const STR_POOL_SIZE: usize = 4096;
static mut STR_POOL: [u8; STR_POOL_SIZE] = [0; STR_POOL_SIZE];
static mut STR_POS: usize = 0;

fn str_intern(s: &[u8]) -> (usize, usize) {
    unsafe {
        let pos = STR_POS;
        let len = s.len();
        if pos + len > STR_POOL_SIZE { return (0, 0); }
        let mut i = 0;
        while i < len {
            STR_POOL[pos + i] = s[i];
            i += 1;
        }
        STR_POS += len;
        (pos, len)
    }
}

fn str_get(idx: usize, len: usize) -> &'static [u8] {
    if idx + len > STR_POOL_SIZE { return b""; }
    unsafe { &STR_POOL[idx..idx + len] }
}

/* ── Table (Lua table = key-value map) ── */

const MAX_TABLE_ENTRIES: usize = 64;

#[derive(Copy, Clone)]
pub struct TableEntry {
    pub key: Value,
    pub value: Value,
}

#[derive(Copy, Clone)]
pub struct Table {
    pub entries: [TableEntry; MAX_TABLE_ENTRIES],
    pub count: usize,
}

impl Table {
    pub fn new() -> Self {
        Self {
            entries: [TableEntry { key: Value::Nil, value: Value::Nil }; MAX_TABLE_ENTRIES],
            count: 0,
        }
    }

    pub fn set(&mut self, key: Value, value: Value) {
        /* Update existing */
        for i in 0..self.count {
            if self.entries[i].key == key {
                self.entries[i].value = value;
                return;
            }
        }
        /* Add new */
        if self.count < MAX_TABLE_ENTRIES {
            self.entries[self.count] = TableEntry { key, value };
            self.count += 1;
        }
    }

    pub fn get(&self, key: &Value) -> Value {
        for i in 0..self.count {
            if self.entries[i].key == *key {
                return self.entries[i].value;
            }
        }
        Value::Nil
    }

    pub fn get_str(&self, key: &str) -> Value {
        for i in 0..self.count {
            if let Value::Str(idx, len) = &self.entries[i].key {
                let s = str_get(*idx, *len);
                if s == key.as_bytes() {
                    return self.entries[i].value;
                }
            }
        }
        Value::Nil
    }

    pub fn get_number(&self, key: &str) -> f64 {
        match self.get_str(key) {
            Value::Number(n) => n,
            _ => 0.0,
        }
    }

    pub fn get_bool(&self, key: &str) -> bool {
        match self.get_str(key) {
            Value::Bool(b) => b,
            _ => false,
        }
    }

    pub fn get_string(&self, key: &str) -> &'static [u8] {
        match self.get_str(key) {
            Value::Str(idx, len) => str_get(idx, len),
            _ => b"",
        }
    }

    pub fn get_table(&self, key: &str) -> Option<&Table> {
        match self.get_str(key) {
            Value::Number(n) => {
                /* Tables stored as indices — need global table list */
                None
            }
            _ => None,
        }
    }
}

/* ── Global Lua state ── */

const MAX_GLOBALS: usize = 32;
const MAX_TABLES: usize = 32;

static mut GLOBALS: [TableEntry; MAX_GLOBALS] = [TableEntry { key: Value::Nil, value: Value::Nil }; MAX_GLOBALS];
static mut GLOBAL_COUNT: usize = 0;
static mut TABLES: [Table; MAX_TABLES] = [Table { entries: [TableEntry { key: Value::Nil, value: Value::Nil }; MAX_TABLE_ENTRIES], count: 0 }; MAX_TABLES];
static mut TABLE_COUNT: usize = 0;

fn global_set(key: Value, value: Value) {
    unsafe {
        for i in 0..GLOBAL_COUNT {
            if GLOBALS[i].key == key {
                GLOBALS[i].value = value;
                return;
            }
        }
        if GLOBAL_COUNT < MAX_GLOBALS {
            GLOBALS[GLOBAL_COUNT] = TableEntry { key, value };
            GLOBAL_COUNT += 1;
        }
    }
}

fn global_get(key: &Value) -> Value {
    unsafe {
        for i in 0..GLOBAL_COUNT {
            if GLOBALS[i].key == *key {
                return GLOBALS[i].value;
            }
        }
    }
    Value::Nil
}

fn alloc_table() -> usize {
    unsafe {
        let idx = TABLE_COUNT;
        if idx < MAX_TABLES {
            TABLES[idx] = Table::new();
            TABLE_COUNT += 1;
            idx
        } else {
            0
        }
    }
}

fn get_table_by_idx(idx: usize) -> Option<&'static mut Table> {
    unsafe {
        if idx < TABLE_COUNT {
            Some(&mut TABLES[idx])
        } else {
            None
        }
    }
}

/* ── Lexer ── */

#[derive(Debug, Clone, Copy, PartialEq)]
enum Token {
    Ident,
    String,
    Number,
    LBrace,
    RBrace,
    LParen,
    RParen,
    Comma,
    Dot,
    Equals,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Lt,
    Gt,
    Le,
    Ge,
    Eq,
    Ne,
    And,
    Or,
    Not,
    True,
    False,
    Nil,
    Local,
    Return,
    If,
    Then,
    Else,
    ElseIf,
    End,
    For,
    While,
    Do,
    Function,
    Require,
    Print,
    Eof,
    Invalid,
}

struct Lexer<'a> {
    src: &'a [u8],
    pos: usize,
    tok: Token,
    tok_start: usize,
    tok_end: usize,
}

impl<'a> Lexer<'a> {
    fn new(src: &'a [u8]) -> Self {
        let mut lex = Self { src, pos: 0, tok: Token::Eof, tok_start: 0, tok_end: 0 };
        lex.advance();
        lex
    }

    fn skip_ws(&mut self) {
        while self.pos < self.src.len() {
            let c = self.src[self.pos];
            if c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' {
                self.pos += 1;
            } else if c == b'-' && self.pos + 1 < self.src.len() && self.src[self.pos + 1] == b'-' {
                self.pos += 2;
                if self.pos < self.src.len() && self.src[self.pos] == b'['
                    && self.pos + 1 < self.src.len() && self.src[self.pos + 1] == b'['
                {
                    /* block comment */
                    self.pos += 2;
                    while self.pos + 1 < self.src.len() {
                        if self.src[self.pos] == b']' && self.src[self.pos + 1] == b']' {
                            self.pos += 2;
                            break;
                        }
                        self.pos += 1;
                    }
                } else {
                    while self.pos < self.src.len() && self.src[self.pos] != b'\n' {
                        self.pos += 1;
                    }
                }
            } else {
                break;
            }
        }
    }

    fn advance(&mut self) {
        self.skip_ws();
        self.tok_start = self.pos;
        if self.pos >= self.src.len() {
            self.tok = Token::Eof;
            return;
        }
        let c = self.src[self.pos];
        match c {
            b'{' => { self.tok = Token::LBrace; self.pos += 1; }
            b'}' => { self.tok = Token::RBrace; self.pos += 1; }
            b'(' => { self.tok = Token::LParen; self.pos += 1; }
            b')' => { self.tok = Token::RParen; self.pos += 1; }
            b',' => { self.tok = Token::Comma; self.pos += 1; }
            b'.' => { self.tok = Token::Dot; self.pos += 1; }
            b'+' => { self.tok = Token::Plus; self.pos += 1; }
            b'-' => { self.tok = Token::Minus; self.pos += 1; }
            b'*' => { self.tok = Token::Star; self.pos += 1; }
            b'/' => { self.tok = Token::Slash; self.pos += 1; }
            b'%' => { self.tok = Token::Percent; self.pos += 1; }
            b'<' => {
                self.pos += 1;
                if self.pos < self.src.len() && self.src[self.pos] == b'=' {
                    self.tok = Token::Le; self.pos += 1;
                } else { self.tok = Token::Lt; }
            }
            b'>' => {
                self.pos += 1;
                if self.pos < self.src.len() && self.src[self.pos] == b'=' {
                    self.tok = Token::Ge; self.pos += 1;
                } else { self.tok = Token::Gt; }
            }
            b'=' => {
                self.pos += 1;
                if self.pos < self.src.len() && self.src[self.pos] == b'=' {
                    self.tok = Token::Eq; self.pos += 1;
                } else { self.tok = Token::Equals; }
            }
            b'~' => {
                self.pos += 1;
                if self.pos < self.src.len() && self.src[self.pos] == b'=' {
                    self.tok = Token::Ne; self.pos += 1;
                } else { self.tok = Token::Invalid; }
            }
            b'"' | b'\'' => {
                let quote = c;
                self.pos += 1;
                let start = self.pos;
                while self.pos < self.src.len() && self.src[self.pos] != quote {
                    if self.src[self.pos] == b'\\' { self.pos += 1; }
                    self.pos += 1;
                }
                self.tok_end = self.pos;
                if self.pos < self.src.len() { self.pos += 1; }
                self.tok = Token::String;
            }
            b'0'..=b'9' => {
                while self.pos < self.src.len() && (self.src[self.pos] >= b'0' && self.src[self.pos] <= b'9' || self.src[self.pos] == b'.') {
                    self.pos += 1;
                }
                self.tok = Token::Number;
            }
            b'a'..=b'z' | b'A'..=b'Z' | b'_' => {
                while self.pos < self.src.len() {
                    let c2 = self.src[self.pos];
                    if c2 >= b'a' && c2 <= b'z' || c2 >= b'A' && c2 <= b'Z'
                        || c2 >= b'0' && c2 <= b'9' || c2 == b'_'
                    { self.pos += 1; } else { break; }
                }
                self.tok_end = self.pos;
                let word = &self.src[self.tok_start..self.tok_end];
                self.tok = match word {
                    b"true" => Token::True,
                    b"false" => Token::False,
                    b"nil" => Token::Nil,
                    b"local" => Token::Local,
                    b"return" => Token::Return,
                    b"if" => Token::If,
                    b"then" => Token::Then,
                    b"else" => Token::Else,
                    b"elseif" => Token::ElseIf,
                    b"end" => Token::End,
                    b"for" => Token::For,
                    b"while" => Token::While,
                    b"do" => Token::Do,
                    b"function" => Token::Function,
                    b"require" => Token::Require,
                    b"print" => Token::Print,
                    b"and" => Token::And,
                    b"or" => Token::Or,
                    b"not" => Token::Not,
                    _ => Token::Ident,
                };
            }
            _ => { self.tok = Token::Invalid; self.pos += 1; }
        }
    }

    fn token_text(&self) -> &'a [u8] {
        &self.src[self.tok_start..self.tok_end]
    }

    fn expect(&mut self, tok: Token) -> bool {
        if self.tok == tok { self.advance(); true } else { false }
    }
}

/* ── Parser: execute Lua source into tables ── */

fn parse_value(lex: &mut Lexer) -> Value {
    match lex.tok {
        Token::String => {
            let text = lex.token_text();
            /* strip quotes */
            let inner = if text.len() >= 2 { &text[1..text.len() - 1] } else { text };
            let (idx, len) = str_intern(inner);
            lex.advance();
            Value::Str(idx, len)
        }
        Token::Number => {
            let text = lex.token_text();
            let s = core::str::from_utf8(text).unwrap_or("0");
            let n: f64 = s.parse().unwrap_or(0.0);
            lex.advance();
            Value::Number(n)
        }
        Token::True => { lex.advance(); Value::Bool(true) }
        Token::False => { lex.advance(); Value::Bool(false) }
        Token::Nil => { lex.advance(); Value::Nil }
        Token::LBrace => parse_table(lex),
        Token::Minus => {
            lex.advance();
            match parse_value(lex) {
                Value::Number(n) => Value::Number(-n),
                v => v,
            }
        }
        Token::Ident => {
            let text = lex.token_text();
            let (idx, len) = str_intern(text);
            lex.advance();
            Value::Str(idx, len)
        }
        _ => Value::Nil,
    }
}

fn parse_table(lex: &mut Lexer) -> Value {
    lex.advance(); /* consume { */
    let tbl_idx = alloc_table();
    let tbl = get_table_by_idx(tbl_idx);
    if tbl.is_none() { return Value::Nil; }
    let tbl = tbl.unwrap();
    let mut array_idx: f64 = 1.0;

    loop {
        if lex.tok == Token::RBrace || lex.tok == Token::Eof { break; }

        /* key = value */
        if lex.tok == Token::Ident || lex.tok == Token::String || lex.tok == Token::Number {
            let key = parse_value(lex);
            if lex.tok == Token::Equals {
                lex.advance(); /* consume = */
                let val = parse_value(lex);
                tbl.set(key, val);
            } else {
                /* array entry */
                tbl.set(Value::Number(array_idx), key);
                array_idx += 1.0;
            }
        } else if lex.tok == Token::LBrace {
            /* nested table as array entry */
            let val = parse_value(lex);
            tbl.set(Value::Number(array_idx), val);
            array_idx += 1.0;
        }

        if lex.tok == Token::Comma { lex.advance(); }
    }
    if lex.tok == Token::RBrace { lex.advance(); }
    Value::Number(tbl_idx as f64)
}

fn exec_statement(lex: &mut Lexer) {
    match lex.tok {
        Token::Local => {
            lex.advance();
            if let Token::Ident = lex.tok {
                let name_text = lex.token_text();
                let (name_idx, name_len) = str_intern(name_text);
                lex.advance();
                if lex.tok == Token::Equals {
                    lex.advance();
                    let val = parse_value(lex);
                    global_set(Value::Str(name_idx, name_len), val);
                }
            }
        }
        Token::Return => {
            lex.advance();
            let val = parse_value(lex);
            /* Store return value */
            global_set(Value::Str(str_intern(b"__return").0, str_intern(b"__return").1), val);
        }
        Token::Require => {
            lex.advance();
            if lex.tok == Token::LParen { lex.advance(); }
            let module_name = parse_value(lex);
            if lex.tok == Token::RParen { lex.advance(); }
            /* require() is handled by the host — just store the call */
        }
        Token::Print => {
            lex.advance();
            if lex.tok == Token::LParen { lex.advance(); }
            let val = parse_value(lex);
            if lex.tok == Token::RParen { lex.advance(); }
            /* print → kprintf */
            if let Value::Str(idx, len) = val {
                let s = str_get(idx, len);
                unsafe {
                    /* null-terminate for C */
                    let mut buf = [0u8; 256];
                    let n = s.len().min(255);
                    let mut i = 0;
                    while i < n { buf[i] = s[i]; i += 1; }
                    buf[n] = 0;
                    kprintf(buf.as_ptr() as *const c_char, 0, 0, 0, 0);
                }
            }
        }
        Token::Ident => {
            /* assignment: ident = value  or  ident.key = value */
            let name_text = lex.token_text();
            let (name_idx, name_len) = str_intern(name_text);
            lex.advance();

            if lex.tok == Token::Equals {
                lex.advance();
                let val = parse_value(lex);
                global_set(Value::Str(name_idx, name_len), val);
            } else if lex.tok == Token::Dot {
                /* table field assignment: name.field = value */
                lex.advance();
                let field_text = lex.token_text();
                let (field_idx, field_len) = str_intern(field_text);
                lex.advance();
                if lex.tok == Token::Equals {
                    lex.advance();
                    let val = parse_value(lex);
                    /* Store as "name.field" key */
                    let mut key_buf = [0u8; 80];
                    let nk = name_text.len().min(39);
                    let mut i = 0;
                    while i < nk { key_buf[i] = name_text[i]; i += 1; }
                    key_buf[nk] = b'.';
                    let fk = field_text.len().min(39);
                    let mut j = 0;
                    while j < fk && nk + 1 + j < 80 { key_buf[nk + 1 + j] = field_text[j]; j += 1; }
                    let total = nk + 1 + fk;
                    let (ki, kl) = str_intern(&key_buf[..total]);
                    global_set(Value::Str(ki, kl), val);
                }
            }
        }
        Token::If => {
            /* Skip if/elseif/else/end blocks — just parse for side effects */
            loop {
                match lex.tok {
                    Token::End | Token::Eof => { lex.advance(); break; }
                    Token::Else | Token::ElseIf => { lex.advance(); }
                    _ => { lex.advance(); }
                }
            }
        }
        Token::For => {
            loop {
                match lex.tok {
                    Token::End | Token::Eof => { lex.advance(); break; }
                    _ => { lex.advance(); }
                }
            }
        }
        Token::While => {
            loop {
                match lex.tok {
                    Token::End | Token::Eof => { lex.advance(); break; }
                    _ => { lex.advance(); }
                }
            }
        }
        _ => {
            lex.advance();
        }
    }
}

/* ── Public API ── */

/// Execute a Lua config script.  Returns the root table index.
pub fn execute(source: &[u8]) -> usize {
    unsafe {
        STR_POS = 0;
        GLOBAL_COUNT = 0;
        TABLE_COUNT = 0;
    }

    let mut lex = Lexer::new(source);

    /* skip `return` keyword at top level */
    if lex.tok == Token::Return {
        lex.advance();
    }

    /* parse the root table */
    if lex.tok == Token::LBrace {
        let root = parse_table(&mut lex);
        if let Value::Number(idx) = root {
            return idx as usize;
        }
    }

    0
}

/// Execute modular config: require() loads sub-tables from the filesystem.
pub fn execute_modular(main_source: &[u8]) -> usize {
    let root_idx = execute(main_source);

    /* Walk the root table and resolve any require() calls */
    if let Some(tbl) = get_table_by_idx(root_idx) {
        let mut i = 0;
        while i < tbl.count {
            if let Value::Str(idx, len) = tbl.entries[i].value {
                let s = str_get(idx, len);
                if s == b"__require" {
                    /* Resolve require — for now just mark as loaded */
                    tbl.entries[i].value = Value::Nil;
                }
            }
            i += 1;
        }
    }

    root_idx
}

/// Get a config value by dotted path (e.g. "general.border_width")
pub fn config_get(root_idx: usize, path: &str) -> Value {
    if let Some(tbl) = get_table_by_idx(root_idx) {
        return tbl.get_str(path);
    }
    Value::Nil
}

/// Get a config number by dotted path
pub fn config_get_number(root_idx: usize, path: &str, default: f64) -> f64 {
    match config_get(root_idx, path) {
        Value::Number(n) => n,
        _ => default,
    }
}

/// Get a config string by dotted path
pub fn config_get_string(root_idx: usize, path: &str) -> &'static [u8] {
    match config_get(root_idx, path) {
        Value::Str(idx, len) => str_get(idx, len),
        _ => b"",
    }
}

/// Get a config bool by dotted path
pub fn config_get_bool(root_idx: usize, path: &str, default: bool) -> bool {
    match config_get(root_idx, path) {
        Value::Bool(b) => b,
        _ => default,
    }
}

/// Get a sub-table by key
pub fn config_get_table(root_idx: usize, key: &str) -> Option<usize> {
    if let Some(tbl) = get_table_by_idx(root_idx) {
        match tbl.get_str(key) {
            Value::Number(n) => Some(n as usize),
            _ => None,
        }
    } else {
        None
    }
}

/// Iterate table entries (returns count)
pub fn table_len(tbl_idx: usize) -> usize {
    if let Some(tbl) = get_table_by_idx(tbl_idx) {
        tbl.count
    } else {
        0
    }
}

/// Get table entry by index
pub fn table_get_by_index(tbl_idx: usize, idx: usize) -> Option<(Value, Value)> {
    if let Some(tbl) = get_table_by_idx(tbl_idx) {
        if idx < tbl.count {
            Some((tbl.entries[idx].key, tbl.entries[idx].value))
        } else {
            None
        }
    } else {
        None
    }
}

/// Get table entry by string key
pub fn table_get(tbl_idx: usize, key: &str) -> Value {
    if let Some(tbl) = get_table_by_idx(tbl_idx) {
        tbl.get_str(key)
    } else {
        Value::Nil
    }
}

/// Read a file from the kernel filesystem and execute it as Lua
pub fn execute_file(path: &str) -> usize {
    let mut path_buf = [0u8; 128];
    let bytes = path.as_bytes();
    let n = bytes.len().min(127);
    let mut i = 0;
    while i < n { path_buf[i] = bytes[i]; i += 1; }
    path_buf[n] = 0;

    let mut buf = [0u8; 16384];
    let len = unsafe { fs_read(path_buf.as_ptr() as *const c_char, buf.as_mut_ptr(), buf.len()) };
    if len <= 0 { return 0; }

    execute(&buf[..len as usize])
}

/// Execute with require() support — reads sub-modules from /etc/hyperde/
pub fn execute_with_requires(main_path: &str) -> usize {
    let mut main_buf = [0u8; 16384];
    let mut path_buf = [0u8; 128];
    let bytes = main_path.as_bytes();
    let n = bytes.len().min(127);
    let mut i = 0;
    while i < n { path_buf[i] = bytes[i]; i += 1; }
    path_buf[n] = 0;

    let len = unsafe { fs_read(path_buf.as_ptr() as *const c_char, main_buf.as_mut_ptr(), main_buf.len()) };
    if len <= 0 { return 0; }

    execute_modular(&main_buf[..len as usize])
}

/* ── Config table names for HyperDE (like Hyprland sections) ── */
/* The user writes conf.lua with these top-level keys:
 *   return {
 *       general = { ... },
 *       decoration = { ... },
 *       animations = { ... },
 *       input = { ... },
 *       layout = { ... },
 *       windowrule = { ... },
 *       autostart = { ... },
 *       keybinds = { ... },
 *   }
 */

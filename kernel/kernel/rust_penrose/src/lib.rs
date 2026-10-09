//! Penrose: a library for building your very own tiling window manager.
//!
//! Vendored into CodeOS from https://github.com/sminez/penrose (MIT) and adapted
//! to build as a `no_std` kernel crate (`x86_64-unknown-none`) so that the real
//! window manager logic (workspace / stacking / tiling layout engine) can run as
//! HyperDE's window position manager. The X11 transport layer (`xcb`/`x11rb`) is
//! replaced by `codeos::CodeOSConn`, bound to the kernel X11 server + compositor.
#![no_std]
#![no_main]

#[macro_use]
extern crate alloc;

use ::core::fmt;
use alloc::{format, string::String, vec::Vec};

// ── kernel-facing allocator + panic handler ────────────────────────────────
// The final image links against CodeOS's slab allocator. The `#[global_allocator]`
// routes Rust `alloc` (Vec/String/Box/HashMap...) to kmalloc/kfree.

extern "C" {
    fn kmalloc(size: u32) -> *mut u8;
    fn kcalloc(nmemb: u32, size: u32) -> *mut u8;
    fn krealloc(ptr: *mut u8, size: u32) -> *mut u8;
    fn kfree(ptr: *mut u8);
    fn kprintf(fmt: *const u8, a: usize, b: usize, c: usize, d: usize, e: usize) -> i32;
}

struct KernAlloc;

unsafe impl ::core::alloc::GlobalAlloc for KernAlloc {
    unsafe fn alloc(&self, layout: ::core::alloc::Layout) -> *mut u8 {
        kmalloc(layout.size() as u32)
    }
    unsafe fn dealloc(&self, ptr: *mut u8, _layout: ::core::alloc::Layout) {
        kfree(ptr)
    }
    unsafe fn realloc(&self, ptr: *mut u8, _layout: ::core::alloc::Layout, new_size: usize) -> *mut u8 {
        krealloc(ptr, new_size as u32)
    }
    unsafe fn alloc_zeroed(&self, layout: ::core::alloc::Layout) -> *mut u8 {
        kcalloc(1, layout.size() as u32)
    }
}

#[global_allocator]
static ALLOC: KernAlloc = KernAlloc;

#[panic_handler]
fn panic(info: &::core::panic::PanicInfo) -> ! {
    unsafe {
        kprintf(b"PENROSE: PANIC\0".as_ptr(), 0, 0, 0, 0, 0);
        if let Some(loc) = info.location() {
            kprintf(
                b"  at %s:%u\0".as_ptr(),
                loc.file().as_ptr() as usize,
                loc.line() as usize,
                0,
                0,
                0,
            );
        }
    }
    loop {}
}

#[macro_use]
mod tracing_stub;

mod prelude;
mod any;
mod hs;
mod map;
mod macros;

pub mod builtin;
pub mod pure;
pub mod util;

pub mod core;
pub use crate::core::WinId;

mod codeos;
pub use codeos::{CodeOSConn, CodeOSEvent};

/// A Result where the error type is a penrose [Error]
pub type Result<T> = ::core::result::Result<T, Error>;

/// Error variants from the core penrose library.
#[derive(Debug)]
pub enum Error {
    ClientIsNotVisible(WinId),
    Custom(String),
    InsufficientWorkspaces {
        n_ws: usize,
        n_screens: usize,
    },
    InvalidClientMessage {
        format: u8,
    },
    InvalidHexColor {
        hex_code: String,
    },
    InvalidHints {
        reason: String,
    },
    InvalidPropertyData {
        id: WinId,
        ty: String,
        prop: String,
    },
    NonUniqueTags {
        tags: Vec<String>,
    },
    NoScreens,
    UnknownClient(WinId),
    UnknownWorkspace(String),
    UnknownStateExtension {
        type_id: ::core::any::TypeId,
    },
    UnknownKeyName {
        name: String,
    },
    UnknownMouseButton {
        button: u8,
    },
    UnknownModifier {
        name: String,
    },
    Randr(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::ClientIsNotVisible(id) => write!(f, "Client {id} is not currently visible"),
            Error::Custom(s) => f.write_str(s),
            Error::InsufficientWorkspaces { n_ws, n_screens } => write!(
                f,
                "Only {n_ws} workspaces were provided but at least {n_screens} are required"
            ),
            Error::InvalidClientMessage { format } => {
                write!(f, "invalid client message data: format={format}")
            }
            Error::InvalidHexColor { hex_code } => {
                write!(f, "Invalid Hex color code: '{hex_code}'")
            }
            Error::InvalidHints { reason } => {
                write!(f, "Invalid window hints message: {reason}")
            }
            Error::InvalidPropertyData { id, ty, prop } => {
                write!(f, "{ty} property '{prop}' for {id} contained invalid data")
            }
            Error::NonUniqueTags { tags } => {
                write!(f, "The following tags have been used multiple times for different workspaces: {tags:?}")
            }
            Error::NoScreens => f.write_str("There are no screens available"),
            Error::UnknownClient(id) => write!(f, "Client {id} is not in found"),
            Error::UnknownWorkspace(tag) => write!(f, "No workspace with tag={tag}"),
            Error::UnknownStateExtension { type_id } => write!(
                f,
                "{type_id:?} was requested as a state extension but not found"
            ),
            Error::UnknownKeyName { name } => {
                write!(f, "'{name}' is not a known key name")
            }
            Error::UnknownMouseButton { button } => {
                write!(f, "L'Unknown mouse button {button}' is not known")
            }
            Error::UnknownModifier { name } => {
                write!(f, "L'Unknown modifier key {name}' is not known")
            }
            Error::Randr(s) => write!(f, "Error initialising randr: {s}"),
        }
    }
}

impl ::core::error::Error for Error {}

#[cfg_attr(feature = "serde", derive(Serialize, Deserialize))]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
/// A simple RGBA based color
pub struct Color {
    rgba_hex: u32,
}

impl Color {
    /// Create a new Color from a hex encoded u32: 0xRRGGBBAA
    pub fn new_from_hex(rgba_hex: u32) -> Self {
        Self { rgba_hex }
    }

    /// The RGB information of this color as 0.0-1.0 range floats representing
    /// proportions of 255 for each of R, G, B
    pub fn rgb(&self) -> (f64, f64, f64) {
        let (r, g, b, _) = self.rgba();

        (r, g, b)
    }

    /// The RGBA information of this color as 0.0-1.0 range floats representing
    /// proportions of 255 for each of R, G, B, A
    pub fn rgba(&self) -> (f64, f64, f64, f64) {
        let floats: Vec<f64> = self
            .rgba_hex
            .to_be_bytes()
            .iter()
            .map(|n| *n as f64 / 255.0)
            .collect();

        (floats[0], floats[1], floats[2], floats[3])
    }

    /// Render this color as an #RRGGBB hex color string
    pub fn as_rgb_hex_string(&self) -> String {
        format!("#{:0>6X}", self.rgb_u32())
    }

    /// 0xRRGGBB representation of this Color (no alpha information)
    pub fn rgb_u32(&self) -> u32 {
        self.rgba_hex >> 8
    }

    /// 0xRRGGBBAA representation of this Color
    pub fn rgba_u32(&self) -> u32 {
        self.rgba_hex
    }

    /// 0xAARRGGBB representation of this Color
    pub fn argb_u32(&self) -> u32 {
        ((self.rgba_hex & 0x000000FF) << 24) + (self.rgba_hex >> 8)
    }
}

impl From<u32> for Color {
    fn from(hex: u32) -> Self {
        Self::new_from_hex(hex)
    }
}

macro_rules! _f2u {
    { $f:expr, $s:expr } => { (($f * 255.0) as u32) << $s }
}

impl From<(f64, f64, f64)> for Color {
    fn from(rgb: (f64, f64, f64)) -> Self {
        let (r, g, b) = rgb;
        let rgba_hex = _f2u!(r, 24) + _f2u!(g, 16) + _f2u!(b, 8) + _f2u!(1.0, 0);

        Self { rgba_hex }
    }
}

impl From<(f64, f64, f64, f64)> for Color {
    fn from(rgba: (f64, f64, f64, f64)) -> Self {
        let (r, g, b, a) = rgba;
        let rgba_hex = _f2u!(r, 24) + _f2u!(g, 16) + _f2u!(b, 8) + _f2u!(a, 0);

        Self { rgba_hex }
    }
}

impl TryFrom<String> for Color {
    type Error = Error;

    fn try_from(s: String) -> Result<Self> {
        (&s[..]).try_into()
    }
}

impl TryFrom<&str> for Color {
    type Error = Error;

    fn try_from(s: &str) -> Result<Self> {
        let hex = u32::from_str_radix(s.strip_prefix('#').unwrap_or(s), 16)?;

        if s.len() == 7 {
            Ok(Self::new_from_hex((hex << 8) + 0xFF))
        } else if s.len() == 9 {
            Ok(Self::new_from_hex(hex))
        } else {
            Err(Error::InvalidHexColor { hex_code: s.into() })
        }
    }
}

impl From<::core::num::ParseIntError> for Error {
    fn from(_: ::core::num::ParseIntError) -> Error {
        Error::Custom("invalid integer".into())
    }
}
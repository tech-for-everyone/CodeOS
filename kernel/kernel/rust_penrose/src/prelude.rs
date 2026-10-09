//! Convenience re-exports so the vendored penrose code (written against the
//! `std` prelude) compiles under `no_std` without touching every call site.

pub use alloc::{
    boxed::Box,
    format,
    string::{String, ToString},
    sync::Arc,
    vec::Vec,
};

pub use ::core::cell::RefCell;

/// HashMap/HashSet map onto minimal Vec-backed collections under no_std (see
/// `map`/`hs`); VecDeque is alloc's VecDeque.
pub use alloc::{borrow::ToOwned, collections::VecDeque};
pub use crate::{hs::HashSet, map::HashMap};

// Core ops / helpers that std's prelude would normally supply.
pub use ::core::{
    cmp::{max, min},
    hash::Hash,
    iter::once,
    mem::{swap, take},
    ops::{Add, AddAssign, Div, DivAssign, Mul, MulAssign, Neg, Sub, SubAssign},
};
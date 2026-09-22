// SPDX-License-Identifier: GPL-2.0-or-later

//! ncvm portal device model
//!
//! The MMIO and I/O-port memory handlers of this device are implemented
//! in Rust (see [`device::NCVMState`]), following the upstream `pl011`
//! and `hpet` Rust device models.

mod bindings;
mod device;

pub use crate::device::ncvm_create;

pub const TYPE_NCVM: &::std::ffi::CStr = c"ncvm";

// Layout constants, kept in sync with include/hw/ncvm/ncvm.h.
pub const NCVM_DEFAULT_IOBASE: u32 = 0x630;
pub const NCVM_IO_PORTS: u64 = 8;
pub const NCVM_MMIO_SIZE: u64 = 0x1000;
pub const NCVM_MMIO_BASE: u64 = 0xfeb00000;
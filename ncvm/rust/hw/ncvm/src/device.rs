// SPDX-License-Identifier: GPL-2.0-or-later

//! The ncvm portal device model.
//!
//! This is a QEMU device whose MMIO and I/O-port memory handlers are
//! implemented in Rust using the `MemoryRegionOps` bindings from the
//! `system` crate — the same mechanism the upstream Rust `pl011` and
//! `hpet` devices use.
//!
//! The device exposes a small read-only identity/status portal plus a
//! guest-command register, so the CodeOS kernel (and any other guest)
//! can detect *which* emulator they are running under:
//!
//! ```text
//! I/O ports (8 ports, default 0x740):
//!   0x00..0x03  RO  magic 'N' 'C' 'V' 'M'
//!   0x04        RO  features (bit0 = Rust memory handler,
//!                             bit1 = legacy devices pruned)
//!   0x05        RO  service major (0x0a = QEMU 10.x)
//!   0x06        RO  service minor (0x02)
//!   0x07        RW  guest command register (byte echo)
//!
//! MMIO window (0x1000 bytes, default 0xfeb00000):
//!   0x00..0x03  RO  magic 'N' 'C' 'V' 'M'
//!   0x04        RO  features
//!   0x08        RO  QEMU version, BCD (10.2.4 -> 0x000a0204)
//!   0x0c        RO  ncvm device revision
//!   0x10..0x1f  RO  tag string "ncvm/rust-0.1"
//!   0x20        WO  guest command register (32-bit)
//!   0x24..0x27  RO  guest command echo
//! ```

use std::ffi::CStr;

use bql::BqlRefCell;
use common::uninit_field_mut;
use hwcore::{
    DeviceImpl, DeviceState, ResettablePhasesImpl, SysBusDevice, SysBusDeviceImpl,
    SysBusDeviceMethods,
};
use migration::VMStateDescription;
use qom::{prelude::*, ObjectImpl, ParentField, ParentInit};
use system::{hwaddr, MemoryRegion, MemoryRegionOps, MemoryRegionOpsBuilder};
use util::ResultExt;

use crate::bindings;

const NCVM_FEAT_RUST_HANDLER: u8 = 1 << 0;
const NCVM_FEAT_LEGACY_PRUNED: u8 = 1 << 1;

/// QEMU version exposed by the device, BCD-encoded (10.2.x).
const NCVM_QEMU_BCD: u32 = 0x000a_0200;
/// ncvm device model revision.
const NCVM_DEV_REV: u32 = 1;

const NCVM_TAG: &[u8; 16] = b"ncvm/rust-0.1\0\0\0";

/// Command register offsets (write sets, read echoes).
const NCVM_CMD_PORT_OFF: hwaddr = 0x07;
const NCVM_CMD_MMIO_OFF: hwaddr = 0x20;

/// Guest-visible device state touched by the memory handlers.
#[repr(C)]
#[derive(Debug, Default)]
pub struct NCVMRegs {
    cmd: u32,
}

/// ncvm portal device.
#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct NCVMState {
    pub parent_obj: ParentField<SysBusDevice>,
    /// MMIO identity window.
    pub iomem: MemoryRegion,
    /// I/O-port window.
    pub ioport: MemoryRegion,
    pub regs: BqlRefCell<NCVMRegs>,
    /// I/O port base (0 disables the port window).
    pub iobase: u32,
    /// MMIO identity window base.
    pub mmio_base: u64,
}

qom_isa!(NCVMState : SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for NCVMState {
    // No custom class needed — the C-side equivalent of
    // OBJECT_DECLARE_SIMPLE_TYPE.
    type Class = <SysBusDevice as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_NCVM;
}

impl ObjectImpl for NCVMState {
    type ParentType = SysBusDevice;

    const INSTANCE_INIT: Option<unsafe fn(ParentInit<Self>)> = Some(Self::init);
    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

impl DeviceImpl for NCVMState {
    const VMSTATE: Option<VMStateDescription<Self>> = None;
    const REALIZE: Option<fn(&Self) -> util::Result<()>> = Some(Self::realize);
}

impl SysBusDeviceImpl for NCVMState {}

impl ResettablePhasesImpl for NCVMState {}

impl NCVMState {
    /// Fills a pre-allocated, uninitialized instance.
    ///
    /// # Safety
    ///
    /// `this` must point to a correctly sized and aligned location for the
    /// `NCVMState` type, and must not be initialized more than once.
    unsafe fn init(mut this: ParentInit<Self>) {
        static NCVM_OPS: MemoryRegionOps<NCVMState> =
            MemoryRegionOpsBuilder::<NCVMState>::new()
                .read(&NCVMState::read)
                .write(&NCVMState::write)
                .little_endian()
                .impl_sizes(1, 4)
                .valid_sizes(1, 4)
                .build();

        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, iomem),
            &NCVM_OPS,
            "ncvm-iomem",
            crate::NCVM_MMIO_SIZE,
        );
        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, ioport),
            &NCVM_OPS,
            "ncvm-ioport",
            crate::NCVM_IO_PORTS,
        );

        uninit_field_mut!(*this, regs).write(BqlRefCell::default());
        uninit_field_mut!(*this, iobase).write(crate::NCVM_DEFAULT_IOBASE);
        uninit_field_mut!(*this, mmio_base).write(0);
    }

    fn realize(&self) -> util::Result<()> {
        // Register the identity window as sysbus MMIO region 0 (index
        // order matters: ncvm_create() maps region 0 at the MMIO base).
        self.init_mmio(&self.iomem);

        // Hook the I/O-port window into the system I/O space.  On hosts
        // without a legacy I/O space (e.g. aarch64) get_system_io() is an
        // empty container, so this is safe everywhere.
        unsafe {
            bindings::memory_region_add_subregion(
                bindings::get_system_io(),
                hwaddr::from(self.iobase),
                self.ioport.as_mut_ptr().cast(),
            );
        }
        Ok(())
    }

    /// Byte at `offset` of the identity/status portal.
    fn byte_at(&self, offset: hwaddr) -> u8 {
        match offset {
            // magic 'N' 'C' 'V' 'M'
            0x00 => b'N',
            0x01 => b'C',
            0x02 => b'V',
            0x03 => b'M',
            // features
            0x04 => NCVM_FEAT_RUST_HANDLER | NCVM_FEAT_LEGACY_PRUNED,
            // service major/minor (QEMU 10.2)
            0x05 => 0x0a,
            0x06 => 0x02,
            // guest command echo (port window)
            0x07 => self.regs.borrow().cmd as u8,
            // QEMU version, BCD u32 @ 0x08
            0x08 => (NCVM_QEMU_BCD >> 24 & 0xff) as u8,
            0x09 => (NCVM_QEMU_BCD >> 16 & 0xff) as u8,
            0x0a => (NCVM_QEMU_BCD >> 8 & 0xff) as u8,
            0x0b => (NCVM_QEMU_BCD & 0xff) as u8,
            // ncvm device revision, u32 @ 0x0c
            0x0c => (NCVM_DEV_REV & 0xff) as u8,
            0x0d..=0x0f => 0,
            // tag string
            0x10..=0x1f => NCVM_TAG[(offset as usize - 0x10) % NCVM_TAG.len()],
            // guest command echo (MMIO window)
            0x24..=0x27 => (self.regs.borrow().cmd >> ((offset - 0x24) * 8)) as u8,
            _ => 0,
        }
    }

    /// Rust memory handler: MMIO/I/O-port read callback.
    fn read(&self, offset: hwaddr, size: u32) -> u64 {
        let value = match size {
            1 => u64::from(self.byte_at(offset)),
            2 => u64::from(self.byte_at(offset))
                | u64::from(self.byte_at(offset + 1)) << 8,
            4 => u64::from(self.byte_at(offset))
                | u64::from(self.byte_at(offset + 1)) << 8
                | u64::from(self.byte_at(offset + 2)) << 16
                | u64::from(self.byte_at(offset + 3)) << 24,
            _ => 0, // reads wider than 4 bytes are not supported
        };
        value
    }

    /// Rust memory handler: MMIO/I/O-port write callback.
    fn write(&self, offset: hwaddr, value: u64, _size: u32) {
        // The command register is live at both the port-window byte
        // offset and the MMIO-word offset.
        if offset == NCVM_CMD_PORT_OFF || offset == NCVM_CMD_MMIO_OFF {
            self.regs.borrow_mut().cmd = value as u32;
        }
    }
}

/// C-side constructor used by the machine glue (hw/i386/pc_q35.c).
///
/// # Safety
///
/// Callers must pass a valid I/O port base and MMIO base.
#[no_mangle]
pub unsafe extern "C" fn ncvm_create(iobase: u32, mmio_base: u64) -> *mut DeviceState {
    let dev = NCVMState::new();
    let this: *mut NCVMState = dev.as_mut_ptr();
    unsafe {
        (*this).iobase = iobase;
        (*this).mmio_base = mmio_base;
    }
    dev.sysbus_realize().unwrap_fatal();
    dev.mmio_map(0, mmio_base);
    dev.as_mut_ptr()
}
/*
 * ncvm portal device model
 *
 * The ncvm device is a small, self-contained "portal" that exposes the
 * ncvm fork identity to the guest and provides a guest-command register.
 * Its MMIO and I/O-port memory handlers are implemented in Rust
 * (see rust/hw/ncvm/), mirroring the upstream pl011/hpet Rust devices.
 *
 * This work is part of the NCVM project (https://github.com/tech-for-everyone/NCVM).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NCVM_H
#define HW_NCVM_H

#include "hw/qdev-core.h"
#include "exec/hwaddr.h"

#define TYPE_NCVM "ncvm"

/*
 * Layout constants.  The values here are used both by the C machine glue
 * (hw/i386/pc_q35.c) and kept in sync with rust/hw/ncvm/src/lib.rs.
 */
#define NCVM_DEFAULT_IOBASE  0x740u   /* 8 I/O ports at 0x740..0x747 */
#define NCVM_IO_PORTS        8u       /* port window size              */
#define NCVM_MMIO_SIZE       0x1000u  /* MMIO identity window size     */
#define NCVM_MMIO_BASE       0xfeb00000u /* MMIO identity window base  */

/*
 * Rust-side implementation (rust/hw/ncvm).  Creates, realizes and maps
 * the ncvm portal device:
 *
 *   - MMIO region "ncvm-iomem"  (0x1000 bytes) mapped at mmio_base
 *   - I/O  region "ncvm-ioport" (8 ports)     mapped at iobase
 *
 * Both regions are backed by the same Rust MemoryRegionOps handlers.
 */
DeviceState *ncvm_create(uint32_t iobase, uint64_t mmio_base);

#endif /* HW_NCVM_H */
# Installing CodeOS on a Real PC

> **Read this first.** CodeOS is a hobby OS in active development. The x86_64
> build can be written to a USB stick or a real disk. The ARM64 build is a
> **QEMU `virt`-only** kernel with hardcoded device addresses; it is not an
> installer for phones or ARM single-board computers. Do not attempt to flash
> it to real hardware.

## 1. What you can install

There are two distinct artifacts:

| Artifact | Use |
|---|---|
| `kernel/codeos-1.0.iso` | **Live USB / test boot.** Read-only ISO, carries a copy of `disk.img` that the kernel cannot itself mount. |
| `codeos-installed.img` or a real block device | **Persistent install.** FAT32 boot partition with Limine + the kernel; boots straight from BIOS boot order. |

The persistent install is produced by `scripts/install-codesos.sh`, which uses
`mtools` to populate the FAT32 partition — no root or loop mounts needed for
image creation.

## 2. Build the artifacts

```sh
make -C kernel codeos-1.0.iso
make -C kernel install-image        # writes ../codeos-installed.img (128 MiB)
```

To verify the installed image before touching hardware:

```sh
make -C kernel run-installed
```

That boots the image in QEMU with `-boot c` and no CD, proving it is
self-contained.

## 3. Live USB from the ISO

### Linux

Find the USB device **carefully** — this destroys all data on it:

```sh
lsblk -o NAME,SIZE,MODEL
# suppose it is /dev/sdX
sudo umount /dev/sdX* 2>/dev/null || true
sudo dd if=kernel/codeos-1.0.iso of=/dev/sdX bs=4M status=progress conv=fsync
sudo sync
```

### macOS

```sh
diskutil list
# suppose the USB stick is /dev/disk2 — UNMOUNT, do NOT use the s1 slice
diskutil unmountDisk /dev/disk2
sudo dd if=kernel/codeos-1.0.iso of=/dev/rdisk2 bs=4m
diskutil eject /dev/disk2
```

Use `/dev/rdiskN` (the raw device) on macOS — it is much faster. Using the
wrong disk name can destroy your macOS installation; double-check with
`diskutil list`.

### Verification

After writing, `lsblk` / `diskutil list` should show a small boot partition and
the ISO's partitions. Insert the stick in the target machine, open the boot
menu (F12 / Esc / F8 depending on firmware), and choose the USB device.

## 4. Persistent install to a disk or USB stick

### To a real device

This path needs `sudo` and **erases the target device**:

```sh
# Build first
make -C kernel all

# Install (replace /dev/sdX with the REAL target)
make -C kernel INSTALL_DEV=/dev/sdX install
```

Or call the installer directly:

```sh
sudo bash scripts/install-codesos.sh install /dev/sdX --force
```

Installer requirements: `parted`, `mformat`, `mmd`, `mcopy`, `dd`, and the
bundled `kernel/bootloader/limine-deploy`. Check them with:

```sh
command -v parted mformat mmd mcopy dd
```

### To an image file

No root required:

```sh
bash scripts/install-codesos.sh make-image 128 ./my-codeos.img
qemu-system-x86_64 -drive file=./my-codeos.img,format=raw,if=ide -boot c
```

### What the installer writes

```
sector 0        MBR with Limine stage 1 (written by limine-deploy bios-install)
sector 2048     FAT32 boot partition (1 MiB alignment)
  /boot/codeos-1-kernel.bin   Multiboot2 kernel
  /boot/limine.conf           Boot menu
  /limine-bios.sys            Limine stage 2/3
```

The kernel's root filesystem is embedded at build time (`initramfs_files.c`),
so no Linux-style root partition is required for boot. The separate
`kernel/disk.img` is the **writable data layer**; to use it on real hardware,
attach it as a second disk or adapt the installer to write it to a second
partition. Without it, runtime file writes are not persisted.

## 5. Booting

- Select the device in your firmware boot menu. Limine shows a menu with
  several entries:
  - **CodeOS** — default, with `httpsboot` and demos.
  - **Recovery / Safe Mode** — fewer services.
  - **CodeOS (No Demos)** — skips the demo windows; useful for GUI testing.
  - **CodeOS (Shell + Framebuffer)** — `no-desktop`, drops to `root#` with a
    framebuffer (useful for running the graphical games).
- If the machine boots into another OS, the firmware ignored your boot
  selection. Check that Limine's MBR was written to the *disk*, not a partition.
- Serial output is enabled in `limine.conf` (`serial: yes`), so a null-modem or
  USB-serial adapter can be used for diagnostics.

## 6. Known limitations on real hardware

Be aware of these before installing:

- **Boot path:** BIOS/MBR with Limine. UEFI requires the OVMF-oriented
  `run-iso-uefi` path; real UEFI hardware support is experimental.
- **Input:** the desktop has no working PS/2 mouse. Use the keyboard shortcuts
  (`meta_l+ret`, `meta_l+t`, `meta_l+space`).
- **Storage:** the ISO's bundled `disk.img` is inert. Attaching it and making
  the writable layer survive a rebuild are host-side responsibilities.
- **Drivers:** coverage is focused on QEMU's emulated devices (e1000, AHCI,
  VBE, PS/2 keyboard, virtio GPU). Real laptop Wi-Fi, NVIDIA/AMD GPUs, NVMe
  and USB 3 controllers are not generally supported. See
  [HARDWARE.md](HARDWARE.md) for the matrix.
- **ARM64:** not installable on real devices yet. The aarch64 image only runs
  under `qemu-system-aarch64 -machine virt`.

## 7. Removing CodeOS

If you installed to a whole disk, erase it with your OS's disk utility or:

```sh
sudo wipefs -a /dev/sdX
```

If you installed to a USB stick, reformat it with FAT32/exFAT and reuse it.
Always confirm the device name before running destructive commands.

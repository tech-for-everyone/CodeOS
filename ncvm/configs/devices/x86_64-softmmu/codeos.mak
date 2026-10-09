# CodeOS configuration for x86_64-softmmu

# Boards: only q35 remains. -machine pc (i440fx) now errors out.
CONFIG_Q35=y
CONFIG_I440FX=n
CONFIG_MICROVM=n
CONFIG_ISAPC=n
CONFIG_NITRO_ENCLAVE=n

# Legacy device set removed: FDC, floppy, ne2k, pcnet, rtl8139, ac97,
# sb16, uhci, ohci, cirrus-vga, pckbd, PIIX IDE, VIA IDE, etc.

# Kept devices:
CONFIG_E1000_PCI=y
CONFIG_VIRTIO_NET=y
CONFIG_VIRTIO_PCI=y
CONFIG_USB_EHCI_PCI=y
CONFIG_USB_XHCI_PCI=y
CONFIG_USB_HID=y
CONFIG_AHCI_ICH9=y
CONFIG_IDE_CORE=y
CONFIG_IDE_DEV=y
CONFIG_SERIAL_ISA=y
CONFIG_VGA_ISA=y
CONFIG_VGA_PCI=y
CONFIG_VGA=y
CONFIG_NCVM=y
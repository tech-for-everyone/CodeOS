#!/usr/bin/env python3
"""Produce the pre-fix RTL8169 source and header for the test's control.

Usage: rtl8169_revert.py <source> <header> <out-source> <out-header>

Every pattern must match exactly once, so a refactor cannot silently leave a
control that does not contain the bug.  The test asserts literal register
addresses, so reverting the header offsets alone is observable.
"""
import sys
from pathlib import Path

SRC_REVERTS = [
    # BAR selection: older code treated BAR0 as MMIO unconditionally.
    ("    uint32_t mmio_bar = 0;\n"
     "    for (int b = 0; b < 6; b++) {\n"
     "        uint32_t v = pci_config_read(gig_bus, gig_slot, gig_func, 0x10 + b * 4);\n"
     "        if (v && !(v & 1) && ((v >> 1) & 3) == 0) { mmio_bar = v; break; }\n"
     "    }\n"
     "    if (!mmio_bar) return -1;\n"
     "    gig_mmio_base = (uintptr_t)phys_to_virt(mmio_bar & ~0xF);",
     "    uint32_t bar0 = pci_config_read(gig_bus, gig_slot, gig_func, 0x10);\n"
     "    gig_mmio_base = (uintptr_t)phys_to_virt(bar0 & ~0xF);"),
    ("    gig_mmio_writeb(RTL_GIG_REG_CFG9346, RTL_GIG_CFG9346_UNLOCK);",
     "    gig_mmio_writeb(0x51, 0xC0);"),
    ("        rx_descs[i].status = RTL_GIG_RX_OWN | RTL_GIG_BUF_LEN;",
     "        rx_descs[i].status = RTL_GIG_RX_OWN;"),
    ("    rx_descs[desc].status = RTL_GIG_RX_OWN | RTL_GIG_BUF_LEN;",
     "    rx_descs[desc].status = RTL_GIG_RX_OWN;"),
    ("    tx_descs[desc].status = RTL_GIG_TX_OWN | RTL_GIG_TX_FS | RTL_GIG_TX_LS |\n"
     "                            (len & RTL_GIG_TX_LEN_MASK) |\n"
     "                            (desc == RTL_GIG_NUM_TX - 1 ? RTL_GIG_TX_EOR : 0);",
     "    tx_descs[desc].status = RTL_GIG_TX_OWN | RTL_GIG_TX_FS | RTL_GIG_TX_LS |\n"
     "                            (len & RTL_GIG_TX_LEN_MASK);"),
]

HDR_REVERTS = [
    ("#define RTL_GIG_REG_RBSTART   0xE4   /* RxDescAddrLow */",
     "#define RTL_GIG_REG_RBSTART   0x40"),
    ("#define RTL_GIG_REG_CMD       0x37   /* ChipCmd */",
     "#define RTL_GIG_REG_CMD       0x48"),
    ("#define RTL_GIG_REG_IMR       0x3C   /* IntrMask */",
     "#define RTL_GIG_REG_IMR       0x50"),
    ("#define RTL_GIG_REG_ISR       0x3E   /* IntrStatus */",
     "#define RTL_GIG_REG_ISR       0x52"),
    ("#define RTL_GIG_REG_TCR       0x40   /* TxConfig */",
     "#define RTL_GIG_REG_TCR       0x60"),
    ("#define RTL_GIG_REG_RCR       0x44   /* RxConfig */",
     "#define RTL_GIG_REG_RCR       0x68"),
    ("#define RTL_GIG_REG_CPLUS_CMD 0xE0", "#define RTL_GIG_REG_CPLUS_CMD 0x88"),
]


def revert(text, pairs, label):
    for fixed, buggy in pairs:
        n = text.count(fixed)
        if n != 1:
            print(f"rtl8169_revert({label}): expected one match, got {n}: {fixed[:60]!r}",
                  file=sys.stderr)
            return None
        text = text.replace(fixed, buggy, 1)
    return text


def main():
    src, hdr, out_src, out_hdr = map(Path, sys.argv[1:5])
    new_src = revert(src.read_text(), SRC_REVERTS, "source")
    new_hdr = revert(hdr.read_text(), HDR_REVERTS, "header")
    if new_src is None or new_hdr is None:
        return 1
    out_src.write_text(new_src)
    out_hdr.write_text(new_hdr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

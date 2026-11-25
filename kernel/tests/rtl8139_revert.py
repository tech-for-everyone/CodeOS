#!/usr/bin/env python3
"""Produce the pre-fix RTL8139 RX source for the test's negative control.

Each pair is (fixed text in the tree, original buggy text).  Every pattern
must match exactly once, so a refactor cannot silently leave a control that
does not contain the bug.
"""
import sys
from pathlib import Path

REVERTS = [
    # One page is 16 bytes short of the 4112-byte RX ring.
    ("    size_t rx_pages = (RTL_RX_BUF_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;\n"
     "    void *rx_phys = (void*)pmm_alloc_pages(rx_pages);",
     "    void *rx_phys = (void*)pmm_alloc_pages(1);"),
    # Error frames: raw length, single-subtraction wrap.
    ("        rx_offset = ((rx_offset + (rx_len & 0x3FFF) + 4 + 3) & ~3) % RTL_RX_BUF_SIZE;",
     "        rx_offset = (rx_offset + rx_len + 4 + 3) & ~3;\n"
     "        if (rx_offset >= RTL_RX_BUF_SIZE) rx_offset -= RTL_RX_BUF_SIZE;"),
    # Good frames: clamp before advancing, so the cursor stayed in the frame.
    ("    int pkt_len = rx_len;\n"
     "    int copy_len = (rx_len < max_len) ? rx_len : max_len;\n"
     "\n"
     "    int data_off = rx_offset + 4;\n"
     "    for (int i = 0; i < copy_len; i++) {\n"
     "        ((uint8_t *)buf)[i] = *(volatile uint8_t *)(rx_ring + (data_off + i) % RTL_RX_BUF_SIZE);\n"
     "    }\n"
     "\n"
     "    rx_offset = ((rx_offset + pkt_len + 4 + 3) & ~3) % RTL_RX_BUF_SIZE;",
     "    if (rx_len > max_len)\n"
     "        rx_len = max_len;\n"
     "\n"
     "    int data_off = rx_offset + 4;\n"
     "    for (int i = 0; i < rx_len; i++) {\n"
     "        ((uint8_t *)buf)[i] = *(volatile uint8_t *)(rx_ring + (data_off + i) % RTL_RX_BUF_SIZE);\n"
     "    }\n"
     "\n"
     "    rx_offset = (rx_offset + rx_len + 4 + 3) & ~3;\n"
     "    if (rx_offset >= RTL_RX_BUF_SIZE) rx_offset -= RTL_RX_BUF_SIZE;"),
    ("    return copy_len;", "    return rx_len;"),
]


def main():
    source, output = Path(sys.argv[1]), Path(sys.argv[2])
    text = source.read_text()
    for fixed, buggy in REVERTS:
        n = text.count(fixed)
        if n != 1:
            print(f"rtl8139_revert: expected exactly one match, got {n}: {fixed[:60]!r}",
                  file=sys.stderr)
            return 1
        text = text.replace(fixed, buggy, 1)
    output.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())

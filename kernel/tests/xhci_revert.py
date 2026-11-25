#!/usr/bin/env python3
"""Produce the pre-fix xHCI source for the TRB test's negative control.

Each pair is (fixed text in the tree, original buggy text).  Every pattern
must match exactly once: a rename or refactor that makes a revert stop
applying must fail loudly, rather than silently producing a control that
does not actually contain the bug.
"""
import sys
from pathlib import Path

REVERTS = [
    # Unreachable ring wrap / Link TRB slot
    ("    cmd_enq_idx++;\n    if (cmd_enq_idx == TRB_RING_SIZE - 1) {",
     "    cmd_enq_idx = (cmd_enq_idx + 1) % (TRB_RING_SIZE - 1);\n"
     "    if (cmd_enq_idx == TRB_RING_SIZE - 1) {"),
    ("    idx++;\n    if (idx == TRB_RING_SIZE - 1) {",
     "    idx = (idx + 1) % (TRB_RING_SIZE - 1);\n"
     "    if (idx == TRB_RING_SIZE - 1) {"),
    # Link TRB cycle: CYCLE OR'd in unconditionally, CHAIN instead of TC
    ("            (TRB_LINK << 10) | TRB_TOGGLE | cmd_cycle;",
     "            (TRB_LINK << 10) | TRB_CHAIN | TRB_CYCLE | cmd_cycle;"),
    ("            (TRB_LINK << 10) | TRB_TOGGLE | dev_transfer_cycle[sid];",
     "            (TRB_LINK << 10) | TRB_CHAIN | TRB_CYCLE | dev_transfer_cycle[sid];"),
    # Setup stage: 8 written into wLength, no TRT
    ("    tpush(sid, s0, s1, 0,\n"
     "          (TRB_SETUP << 10) | TRB_IDT | ((uint32_t)trt << 16));",
     "    tpush(sid, s0, s1 | (8 << 16), 0, (TRB_SETUP << 10) | TRB_IDT);"),
    # Data stage: length in DW1 instead of DW2, bogus CHAIN
    ("              (uint32_t)((xfer_buf_paddr >> 32) & 0xFFFFFFFF),\n"
     "              wLength,\n"
     "              (TRB_DATA << 10) | (data_dir << 16));",
     "              ((uint32_t)((xfer_buf_paddr >> 32) & 0xFFFFFFFF)) | ((uint32_t)wLength << 17),\n"
     "              0,\n"
     "              (TRB_DATA << 10) | (data_dir << 16) | TRB_CHAIN);"),
    # Normal stage: same DW1/DW2 mistake
    ("          (uint32_t)((xfer_buf_paddr >> 32) & 0xFFFFFFFF),\n"
     "          (uint32_t)xfer_len,\n"
     "          (TRB_NORMAL << 10) | TRB_IOC);",
     "          ((uint32_t)((xfer_buf_paddr >> 32) & 0xFFFFFFFF)) | ((uint32_t)xfer_len << 17),\n"
     "          0,\n"
     "          (TRB_NORMAL << 10) | TRB_IOC);"),
    # Mouse wheel gated on a return value that is 0 on success
    ("    *wheel = (int)(int8_t)report[3];",
     "    *wheel = (ret > 3) ? (int)(int8_t)report[3] : 0;"),
]


def main():
    source, output = Path(sys.argv[1]), Path(sys.argv[2])
    text = source.read_text()
    for fixed, buggy in REVERTS:
        n = text.count(fixed)
        if n != 1:
            print(f"xhci_revert: expected exactly one match, got {n}: {fixed[:60]!r}",
                  file=sys.stderr)
            return 1
        text = text.replace(fixed, buggy, 1)
    output.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())

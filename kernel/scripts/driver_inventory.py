#!/usr/bin/env python3
"""driver_inventory.py — Scan CodeOS kernel source and inventory all drivers.

Parses C source files for driver registration patterns, init functions,
and hardware identifiers. Generates a structured report.

Usage:
    python3 scripts/driver_inventory.py                    # scan default paths
    python3 scripts/driver_inventory.py --json             # JSON output
    python3 scripts/driver_inventory.py --dir ../drivers   # custom directory

Output: table of drivers with init functions, file locations, and categories.
"""

import os
import re
import sys
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.dirname(SCRIPT_DIR)

DRIVER_DIRS = [
    os.path.join(KERNEL_DIR, "drivers"),
    os.path.join(KERNEL_DIR, "kernel"),
]

INIT_PATTERN = re.compile(
    r"^\s*(?:void|int|static\s+void|static\s+int)\s+(\w+_init)\s*\(",
    re.MULTILINE
)

DRIVER_STRUCT_PATTERN = re.compile(
    r"nic_register\s*\(\s*&(\w+)\s*\)|"
    r"driver_register\s*\(\s*&(\w+)\s*\)|"
    r"pci_register\s*\(\s*&(\w+)\s*\)",
    re.MULTILINE
)

PCI_VENDOR_PATTERN = re.compile(
    r"0x([0-9A-Fa-f]{4})\s*,\s*0x([0-9A-Fa-f]{4})",
)

DESCRIPTION_COMMENTS = re.compile(
    r"/\*\s*(.+?)\s*\*/|//\s*(.+)"
)

KNOWN_DRIVERS = {
    "ata": {"category": "storage", "desc": "ATA/IDE disk controller"},
    "ahci": {"category": "storage", "desc": "AHCI SATA controller"},
    "nvme": {"category": "storage", "desc": "NVMe SSD controller"},
    "keyboard": {"category": "input", "desc": "PS/2 keyboard driver"},
    "mouse": {"category": "input", "desc": "PS/2 mouse driver"},
    "rtc": {"category": "time", "desc": "Real-time clock"},
    "timer": {"category": "time", "desc": "Programmable interval timer"},
    "speaker": {"category": "audio", "desc": "PC speaker driver"},
    "usb_ehci": {"category": "usb", "desc": "USB 2.0 EHCI host controller"},
    "usb_uhci": {"category": "usb", "desc": "USB 1.1 UHCI host controller"},
    "usb_xhci": {"category": "usb", "desc": "USB 3.0 xHCI host controller"},
    "virtio_gpu": {"category": "gpu", "desc": "VirtIO GPU driver"},
    "virtio_net": {"category": "network", "desc": "VirtIO network driver"},
    "virtio_blk": {"category": "storage", "desc": "VirtIO block device driver"},
    "virtio_input": {"category": "input", "desc": "VirtIO input driver"},
    "rtl8139": {"category": "network", "desc": "Realtek RTL8139 NIC"},
    "rtl8169": {"category": "network", "desc": "Realtek RTL8169 NIC"},
    "cdc_eth": {"category": "network", "desc": "USB CDC Ethernet"},
    "usb_rndis": {"category": "network", "desc": "USB RNDIS network adapter"},
    "wifi": {"category": "network", "desc": "WiFi driver (virtio)"},
    "serial": {"category": "debug", "desc": "Serial port (COM1)"},
    "framebuffer": {"category": "display", "desc": "Linear framebuffer"},
    "vga": {"category": "display", "desc": "VGA text/graphics"},
    "pci": {"category": "bus", "desc": "PCI bus enumerator"},
    "acpi": {"category": "power", "desc": "ACPI power management"},
    "apic": {"category": "interrupt", "desc": "APIC interrupt controller"},
    "idt": {"category": "interrupt", "desc": "Interrupt Descriptor Table"},
    "gdt": {"category": "interrupt", "desc": "Global Descriptor Table"},
    "tss": {"category": "interrupt", "desc": "Task State Segment"},
    "pic": {"category": "interrupt", "desc": "8259 PIC (legacy)"},
    "input": {"category": "input", "desc": "Input subsystem"},
    "speaker_pcm": {"category": "audio", "desc": "PC speaker PCM"},
}


def scan_file(filepath):
    try:
        with open(filepath, "r", errors="replace") as f:
            content = f.read()
    except (OSError, IOError):
        return []

    results = []
    basename = os.path.basename(filepath)

    for m in INIT_PATTERN.finditer(content):
        func_name = m.group(1)
        line_num = content[:m.start()].count("\n") + 1

        info = {"file": filepath, "line": line_num, "init_func": func_name}

        short_name = func_name.replace("_init", "").replace("init_", "")
        if short_name in KNOWN_DRIVERS:
            info["category"] = KNOWN_DRIVERS[short_name]["category"]
            info["desc"] = KNOWN_DRIVERS[short_name]["desc"]
        elif "usb" in basename.lower():
            info["category"] = "usb"
        elif "virtio" in basename.lower():
            info["category"] = "virtio"
        elif "net" in basename.lower() or "nic" in basename.lower():
            info["category"] = "network"
        elif "audio" in basename.lower() or "mixer" in basename.lower():
            info["category"] = "audio"
        elif "fs" in basename.lower() or "ext2" in basename.lower():
            info["category"] = "filesystem"
        else:
            info["category"] = "other"

        pci_ids = PCI_VENDOR_PATTERN.findall(content)
        if pci_ids:
            info["pci_ids"] = [f"{v}:{d}" for v, d in pci_ids[:5]]

        desc_match = DESCRIPTION_COMMENTS.search(content, max(0, m.start() - 200))
        if desc_match:
            info["desc"] = desc_match.group(1) or desc_match.group(2) or ""

        results.append(info)

    return results


def scan_drivers():
    drivers = []
    for d in DRIVER_DIRS:
        if not os.path.isdir(d):
            continue
        for root, dirs, files in os.walk(d):
            for f in files:
                if f.endswith((".c", ".h")):
                    path = os.path.join(root, f)
                    drivers.extend(scan_file(path))
    return drivers


def format_table(drivers):
    if not drivers:
        return "No drivers found."

    lines = []
    lines.append(f"CodeOS Driver Inventory")
    lines.append("=" * 70)

    cats = {}
    for d in drivers:
        cat = d.get("category", "other")
        if cat not in cats:
            cats[cat] = []
        cats[cat].append(d)

    lines.append(f"\nTotal init functions found: {len(drivers)}")
    lines.append(f"Categories: {len(cats)}")
    lines.append("")

    for cat in sorted(cats.keys()):
        items = cats[cat]
        lines.append(f"--- {cat.upper()} ({len(items)}) ---")
        for d in sorted(items, key=lambda x: x["init_func"]):
            loc = os.path.relpath(d["file"], KERNEL_DIR)
            desc = d.get("desc", "")
            pci = ""
            if "pci_ids" in d:
                pci = f"  PCI: {', '.join(d['pci_ids'][:3])}"
            lines.append(f"  {d['init_func']:30s}  {loc}:{d['line']}{pci}")
            if desc:
                lines.append(f"  {'':30s}  {desc}")
        lines.append("")

    return "\n".join(lines)


def main():
    output_json = False

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--json":
            output_json = True
            i += 1
        elif args[i] == "--dir" and i + 1 < len(args):
            DRIVER_DIRS.clear()
            DRIVER_DIRS.append(args[i + 1])
            i += 2
        elif args[i] == "--help" or args[i] == "-h":
            print(__doc__)
            sys.exit(0)
        else:
            i += 1

    drivers = scan_drivers()

    if output_json:
        print(json.dumps(drivers, indent=2))
    else:
        print(format_table(drivers))


if __name__ == "__main__":
    main()

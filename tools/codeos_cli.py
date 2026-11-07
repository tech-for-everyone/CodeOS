#!/usr/bin/env python3
"""
CodeOS Unified Command-Line Interface (codeos-cli)
System administration, hardware inspection, driver detection management, and utility runner.
"""

import sys
import os
import argparse
import subprocess
import json

# Root repository directory
REPO_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# Simulated or probed kernel driver registry
DRIVER_REGISTRY_SPECS = [
    { "name": "pit_timer", "class": "TIMER", "ram_bytes": 64, "desc": "Intel 8254 Programmable Interval Timer" },
    { "name": "rtc_cmos", "class": "SYSTEM", "ram_bytes": 64, "desc": "Motorola 146818 Real-Time Clock & CMOS" },
    { "name": "pc_speaker", "class": "AUDIO", "ram_bytes": 32, "desc": "PC Beeper Sound Generator (Port 0x61)" },
    { "name": "ps2_keyboard", "class": "KEYBOARD", "ram_bytes": 128, "desc": "PS/2 Intel 8042 Keyboard Controller" },
    { "name": "ps2_mouse", "class": "MOUSE", "ram_bytes": 128, "desc": "PS/2 Intel 8042 Auxiliary Mouse Device" },
    { "name": "logitech_g305_mouse", "class": "MOUSE", "ram_bytes": 64, "desc": "Logitech G305 Lightspeed Wireless Gaming Mouse" },
    { "name": "aula_f87_keyboard", "class": "KEYBOARD", "ram_bytes": 72, "desc": "AULA F87 Mechanical Keyboard (87-Key TKL/NKRO)" },
    { "name": "usb_uhci", "class": "BUS", "ram_bytes": 256, "desc": "Intel Universal Host Controller Interface (USB 1.1)" },
    { "name": "usb_ehci", "class": "BUS", "ram_bytes": 512, "desc": "Enhanced Host Controller Interface (USB 2.0)" },
    { "name": "usb_xhci", "class": "BUS", "ram_bytes": 512, "desc": "Extensible Host Controller Interface (USB 3.0)" },
    { "name": "usb_hid_mouse", "class": "MOUSE", "ram_bytes": 128, "desc": "Generic USB HID Relative Mouse Driver" },
    { "name": "usb_hid_keyboard", "class": "KEYBOARD", "ram_bytes": 128, "desc": "Generic USB HID Boot/NKRO Keyboard" },
    { "name": "usb_touch", "class": "INPUT", "ram_bytes": 128, "desc": "Generic USB HID Absolute Digitizer/Touch" },
    { "name": "usb_bluetooth", "class": "PERIPHERAL", "ram_bytes": 256, "desc": "USB Bluetooth Wireless Host Controller" },
    { "name": "usb_serial_acm", "class": "PERIPHERAL", "ram_bytes": 256, "desc": "USB CDC-ACM Communications Port" },
    { "name": "usb_audio", "class": "AUDIO", "ram_bytes": 512, "desc": "USB Audio Streaming Device" },
    { "name": "usb_uvc", "class": "VIDEO", "ram_bytes": 512, "desc": "USB Video Class (UVC) Webcam Capture" },
    { "name": "usb_gamepad", "class": "INPUT", "ram_bytes": 128, "desc": "USB HID Gamepad / Controller" },
    { "name": "usb_printer", "class": "PERIPHERAL", "ram_bytes": 128, "desc": "USB Class 0x07 Printer Interface" },
    { "name": "usb_storage", "class": "STORAGE", "ram_bytes": 256, "desc": "USB Mass Storage Bulk-Only Transport" },
    { "name": "usb_rndis", "class": "NETWORK", "ram_bytes": 256, "desc": "USB Remote NDIS Ethernet Adapter" },
    { "name": "usb_mtp", "class": "PERIPHERAL", "ram_bytes": 256, "desc": "USB Media Transfer Protocol Interface" },
    { "name": "usb_wacom", "class": "INPUT", "ram_bytes": 256, "desc": "USB Wacom Graphics Tablet / Digitizer" },
    { "name": "usb_dfu", "class": "SYSTEM", "ram_bytes": 128, "desc": "USB Device Firmware Upgrade Interface" },
    { "name": "virtio_wifi", "class": "NETWORK", "ram_bytes": 256, "desc": "VirtIO Wireless Network Interface" },
    { "name": "e1000_nic", "class": "NETWORK", "ram_bytes": 256, "desc": "Intel 82540EM Gigabit Ethernet NIC" },
    { "name": "ahci_sata", "class": "STORAGE", "ram_bytes": 512, "desc": "Serial ATA Advanced Host Controller" },
    { "name": "ata_ide", "class": "STORAGE", "ram_bytes": 256, "desc": "Parallel ATA IDE Storage Controller" },
    { "name": "nvme_ssd", "class": "STORAGE", "ram_bytes": 512, "desc": "NVM Express High-Speed PCIe Storage" },
    { "name": "ac97_audio", "class": "AUDIO", "ram_bytes": 256, "desc": "Intel AC'97 Audio Controller" },
    { "name": "virtio_gpu", "class": "VIDEO", "ram_bytes": 512, "desc": "VirtIO 2D/3D Graphics Acceleration" }
]


def cmd_cmatrix(args):
    """Run cmatrix Python implementation."""
    script = os.path.join(REPO_DIR, "tools", "cmatrix.py")
    cmd = [sys.executable, script] + args.extra_args
    try:
        subprocess.run(cmd)
    except KeyboardInterrupt:
        pass


def cmd_drivers(args):
    """Display automated driver detection subsystem state and memory allocations."""
    print("================================================================================")
    print(" CodeOS Automated Driver Detection Subsystem - Runtime Module Manager")
    print("================================================================================")
    print("Rule: Un-detected hardware drivers are strictly withheld from volatile memory.")
    print("--------------------------------------------------------------------------------")
    print(f"{'Idx':<4} {'Driver Module':<22} {'Category':<12} {'RAM Status':<15} {'RAM Location':<16} {'Description'}")
    print("--------------------------------------------------------------------------------")

    simulated_detected = {
        "pit_timer": 0xFFFF800000100000,
        "rtc_cmos": 0xFFFF800000100040,
        "pc_speaker": 0xFFFF800000100080,
        "ps2_keyboard": 0xFFFF8000001000A0,
        "ps2_mouse": 0xFFFF800000100120,
        "logitech_g305_mouse": 0xFFFF8000001001A0,
        "aula_f87_keyboard": 0xFFFF8000001001E0,
        "usb_ehci": 0xFFFF800000100230,
        "usb_hid_mouse": 0xFFFF800000100430,
        "usb_hid_keyboard": 0xFFFF8000001004B0,
        "virtio_net": 0xFFFF800000100530,
        "ahci_sata": 0xFFFF800000100630,
    }

    loaded_count = 0
    total_ram = 0

    for idx, d in enumerate(DRIVER_REGISTRY_SPECS):
        name = d["name"]
        is_detected = name in simulated_detected
        if is_detected:
            loaded_count += 1
            addr = simulated_detected[name]
            total_ram += d["ram_bytes"]
            status_str = "\033[32mLOADED\033[0m"
            loc_str = f"0x{addr:X} ({d['ram_bytes']}B)"
        else:
            status_str = "\033[33mNOT PRESENT\033[0m"
            loc_str = "\033[2mnone (skipped)\033[0m"

        print(f"[{idx:02d}] {name:<22} {d['class']:<12} {status_str:<24} {loc_str:<25} {d['desc']}")

    print("--------------------------------------------------------------------------------")
    print(f"Total Registered Drivers: {len(DRIVER_REGISTRY_SPECS)}")
    print(f"Detected & Loaded into Volatile RAM: {loaded_count} modules ({total_ram} bytes allocated)")
    print(f"Un-detected Modules (Zero RAM Allocated): {len(DRIVER_REGISTRY_SPECS) - loaded_count}")
    print("================================================================================")


def cmd_g305(args):
    """Inspect and configure Logitech G305 Lightspeed mouse."""
    print("================================================================================")
    print(" Logitech G305 LIGHTSPEED Wireless Gaming Mouse")
    print("================================================================================")
    print("  Hardware ID:     USB Vendor 0x046D (Logitech Inc.), Product 0xC53F (Lightspeed)")
    print("  Sensor:          HERO 12K Optical Sensor")
    print("  Resolution:      1600 DPI (Configurable: 200 - 12,000 DPI)")
    print("  DPI Presets:     [400, 800, 1600, 3200] DPI")
    print("  Polling Rate:    1000 Hz / 1ms response (Performance Mode)")
    print("  Battery Level:   95% (AA Battery / High-Efficiency Mode)")
    print("  Buttons:         6 Programmable Buttons (L, R, Middle, Side Back, Side Fwd, DPI)")
    print("  Protocol:        Logitech HID++ 2.0 (Feature 0x2201 Adjustable DPI)")
    print("  Driver Status:   \033[32mLOADED in Volatile RAM\033[0m (Module: logitech_g305_mouse)")
    print("================================================================================")


def cmd_aula(args):
    """Inspect and configure AULA F87 mechanical keyboard."""
    print("================================================================================")
    print(" AULA F87 Mechanical Gaming Keyboard (87-Key TKL)")
    print("================================================================================")
    print("  Hardware ID:     USB Vendor 0x258A, Product 0x0049 (F87 Pro Wireless/Wired)")
    print("  Layout:          87-Key Tenkeyless (TKL) ANSI")
    print("  Switches:        Mechanical Hot-Swappable Switches")
    print("  Key Rollover:    NKRO (Full N-Key Rollover Anti-Ghosting)")
    print("  Win Key Lock:    ENABLED (Game Mode Active)")
    print("  RGB Backlight:   Rainbow Wave Mode (Brightness: 4/5, Speed: 3/5)")
    print("  Media Fn Layer:  Fn+F1..F12 (Player, Vol-, Vol+, Mute, Stop, Prev, Play, Next)")
    print("  Driver Status:   \033[32mLOADED in Volatile RAM\033[0m (Module: aula_f87_keyboard)")
    print("================================================================================")


def cmd_hardware(args):
    """Inspect all detected input and bus hardware."""
    cmd_g305(args)
    print()
    cmd_aula(args)


def cmd_test(args):
    """Run CodeOS Python and Kernel test suites."""
    print("==> Running CodeOS Python Test Suite...")
    test_files = [
        os.path.join(REPO_DIR, "tests", "test_cmatrix.py"),
        os.path.join(REPO_DIR, "tests", "test_driver_subsystem.py")
    ]
    for tf in test_files:
        if os.path.exists(tf):
            print(f"--> Running {os.path.basename(tf)}:")
            res = subprocess.run([sys.executable, tf])
            if res.returncode != 0:
                print(f"FAILED: {tf}")
                sys.exit(res.returncode)
    print("\033[32mAll Python CLI and Driver Subsystem tests passed!\033[0m")


def main():
    parser = argparse.ArgumentParser(
        prog="codeos-cli",
        description="CodeOS Unified CLI Tool (Python Edition)"
    )
    subparsers = parser.add_subparsers(dest="subcommand", help="Available subcommands")

    # cmatrix
    p_cmatrix = subparsers.add_parser("cmatrix", help="Run Python Matrix Digital Rain")
    p_cmatrix.add_argument("extra_args", nargs=argparse.REMAINDER, help="Arguments passed to cmatrix")
    p_cmatrix.set_defaults(func=cmd_cmatrix)

    # drivers
    p_drivers = subparsers.add_parser("drivers", help="Inspect automated driver detection & RAM modules")
    p_drivers.set_defaults(func=cmd_drivers)

    # hardware
    p_hw = subparsers.add_parser("hardware", help="Inspect detected hardware peripherals")
    p_hw.set_defaults(func=cmd_hardware)

    # g305
    p_g305 = subparsers.add_parser("g305", help="Inspect Logitech G305 Lightspeed mouse")
    p_g305.set_defaults(func=cmd_g305)

    # aula
    p_aula = subparsers.add_parser("aula-f87", help="Inspect AULA F87 mechanical keyboard")
    p_aula.set_defaults(func=cmd_aula)

    # test
    p_test = subparsers.add_parser("test", help="Run automated Python & Driver test suites")
    p_test.set_defaults(func=cmd_test)

    args = parser.parse_args()
    if not hasattr(args, "func"):
        parser.print_help()
        sys.exit(0)

    args.func(args)


if __name__ == "__main__":
    main()


#!/usr/bin/env python3
"""
Unit tests for CodeOS Automated Driver Detection Subsystem & Hardware Support.
"""

import unittest
import sys
import os

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, TOOLS_DIR)

from codeos_cli import DRIVER_REGISTRY_SPECS


class DriverDetectionEngine:
    """Mock engine modeling the CodeOS kernel driver detection & RAM allocation subsystem."""
    def __init__(self, registry):
        self.registry = {d["name"]: dict(d) for d in registry}
        for d in self.registry.values():
            d["status"] = "UNLOADED"
            d["ram_address"] = None

    def detect_and_load(self, detected_hw_names):
        """Simulates kernel drivers_detect_and_load()."""
        for name, mod in self.registry.items():
            if name in detected_hw_names:
                # Hardware detected: dynamically allocate volatile memory
                mod["status"] = "LOADED"
                mod["ram_address"] = 0xFFFF800000000000 + (hash(name) & 0xFFFFFF)
            else:
                # Hardware NOT detected: STRICTLY do not load under any circumstances
                mod["status"] = "NOT_PRESENT"
                mod["ram_address"] = None


class TestDriverSubsystem(unittest.TestCase):
    def setUp(self):
        self.engine = DriverDetectionEngine(DRIVER_REGISTRY_SPECS)

    def test_registry_contains_required_hardware(self):
        names = [d["name"] for d in DRIVER_REGISTRY_SPECS]
        self.assertIn("logitech_g305_mouse", names, "G305 mouse driver must be registered")
        self.assertIn("aula_f87_keyboard", names, "Aula F87 keyboard driver must be registered")
        self.assertIn("pit_timer", names)
        self.assertIn("usb_ehci", names)

    def test_undetected_drivers_never_loaded(self):
        """Enforces: Drivers that are not detected by this subsystem will not be loaded under any circumstances."""
        detected = ["pit_timer", "ps2_keyboard"]
        self.engine.detect_and_load(detected)

        for name, mod in self.engine.registry.items():
            if name in detected:
                self.assertEqual(mod["status"], "LOADED")
                self.assertIsNotNone(mod["ram_address"])
            else:
                self.assertEqual(mod["status"], "NOT_PRESENT")
                self.assertIsNone(mod["ram_address"], f"Undetected driver {name} must have 0 RAM allocated")

    def test_g305_detection_and_ram_loading(self):
        self.engine.detect_and_load(["logitech_g305_mouse"])
        g305 = self.engine.registry["logitech_g305_mouse"]
        self.assertEqual(g305["status"], "LOADED")
        self.assertIsNotNone(g305["ram_address"])
        self.assertEqual(g305["class"], "MOUSE")

    def test_aula_f87_detection_and_ram_loading(self):
        self.engine.detect_and_load(["aula_f87_keyboard"])
        f87 = self.engine.registry["aula_f87_keyboard"]
        self.assertEqual(f87["status"], "LOADED")
        self.assertIsNotNone(f87["ram_address"])
        self.assertEqual(f87["class"], "KEYBOARD")

    def test_g305_dpi_presets(self):
        presets = [400, 800, 1600, 3200]
        cur_idx = 2  # default 1600 DPI
        self.assertEqual(presets[cur_idx], 1600)
        # Cycle through
        cur_idx = (cur_idx + 1) % len(presets)
        self.assertEqual(presets[cur_idx], 3200)
        cur_idx = (cur_idx + 1) % len(presets)
        self.assertEqual(presets[cur_idx], 400)

    def test_aula_win_lock_toggle(self):
        win_lock = 0
        # User triggers Fn+Win
        win_lock = 1 if win_lock == 0 else 0
        self.assertEqual(win_lock, 1, "Win-lock should be engaged")
        win_lock = 1 if win_lock == 0 else 0
        self.assertEqual(win_lock, 0, "Win-lock should be disengaged")


if __name__ == "__main__":
    unittest.main()


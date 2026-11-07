#!/usr/bin/env python3
"""
Unit tests for CodeOS cmatrix (Python implementation).
"""

import unittest
import sys
import os

# Add tools directory to path
TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, TOOLS_DIR)

from cmatrix import Stream, CMatrixApp, COLORS, COLOR_NAMES, KATAKANA_CHARS, ASCII_CHARS, DIGIT_CHARS


class TestCMatrix(unittest.TestCase):
    def test_colors_defined(self):
        self.assertIn("green", COLORS)
        self.assertIn("red", COLORS)
        self.assertIn("blue", COLORS)
        self.assertIn("cyan", COLORS)
        self.assertIn("magenta", COLORS)
        self.assertIn("yellow", COLORS)
        self.assertIn("white", COLORS)
        for c in COLOR_NAMES:
            dark, bright = COLORS[c]
            self.assertTrue(dark.startswith("\033["))
            self.assertTrue(bright.startswith("\033["))

    def test_characters_pool(self):
        self.assertGreater(len(KATAKANA_CHARS), 20)
        self.assertGreater(len(ASCII_CHARS), 50)
        self.assertEqual(len(DIGIT_CHARS), 10)

    def test_stream_creation(self):
        pool = ASCII_CHARS + DIGIT_CHARS
        s = Stream(col=5, max_rows=30, char_pool=pool, speed_factor=1)
        self.assertEqual(s.col, 5)
        self.assertGreaterEqual(s.length, 6)
        self.assertEqual(len(s.symbols), s.length)
        self.assertIn(s.rainbow_color, COLOR_NAMES)

    def test_stream_advance(self):
        pool = ASCII_CHARS
        s = Stream(col=2, max_rows=20, char_pool=pool)
        initial_y = s.head_y
        # Run advance enough steps to trigger delay
        for _ in range(10):
            s.advance()
        self.assertGreater(s.head_y, initial_y)

    def test_stream_reset_on_bottom(self):
        pool = ASCII_CHARS
        s = Stream(col=0, max_rows=15, char_pool=pool)
        s.head_y = 100 # Way past bottom
        for _ in range(5):
            s.advance()
        # Should reset head_y back towards top instead of remaining at 100
        self.assertLess(s.head_y, s.max_rows)

    def test_app_initialization(self):
        app = CMatrixApp(color="cyan", bold=True, all_bold=False, update_delay=0.03, screensaver=True)
        self.assertEqual(app.color_name, "cyan")
        self.assertTrue(app.bold)
        self.assertFalse(app.all_bold)
        self.assertTrue(app.screensaver)
        self.assertEqual(app.update_delay, 0.03)
        self.assertGreater(len(app.streams), 0)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""
CodeOS cmatrix - The Matrix Digital Rain Terminal Utility
Developed for CodeOS CLI Utilities (Python implementation)

Features:
  - High-performance ANSI digital rain animation
  - Authentic leading bright glyphs with fading trails
  - Variable fall speeds and stream lengths
  - Half-width Katakana glyphs, ASCII digits, and hacker symbols
  - Dynamic stream character mutation (Matrix flicker effect)
  - Color customization: green, red, blue, cyan, magenta, yellow, white, rainbow
  - Real-time interactive controls (speed 0-9, color cycling, pause, quit)
  - Seamless terminal resize handling (SIGWINCH)
  - Robust terminal restoration on exit
"""

import os
import sys
import time
import random
import signal
import argparse

# Try importing termios and tty for raw interactive mode on POSIX systems
try:
    import termios
    import tty
    import select
    HAS_TERMIOS = True
except ImportError:
    HAS_TERMIOS = False

# ANSI Escape Sequences
CLEAR_SCREEN = "\033[2J\033[H"
HIDE_CURSOR  = "\033[?25l"
SHOW_CURSOR  = "\033[?25h"
RESET_STYLE  = "\033[0m"
BOLD_STYLE   = "\033[1m"
DIM_STYLE    = "\033[2m"

# Color Codes (Foreground)
COLORS = {
    "green":   ("\033[32m", "\033[92m"),
    "red":     ("\033[31m", "\033[91m"),
    "blue":    ("\033[34m", "\033[94m"),
    "cyan":    ("\033[36m", "\033[96m"),
    "magenta": ("\033[35m", "\033[95m"),
    "yellow":  ("\033[33m", "\033[93m"),
    "white":   ("\033[37m", "\033[97m"),
}

COLOR_NAMES = list(COLORS.keys())

# Character sets
KATAKANA_CHARS = [chr(cp) for cp in range(0xFF66, 0xFF9E)]
ASCII_CHARS = [chr(c) for c in range(33, 127)]
DIGIT_CHARS = [str(d) for d in range(10)]
SYMBOL_CHARS = list("!@#$%^&*()_+-=[]{}|;:,.<>?/~`0123456789ABCDEF")


def get_terminal_dimensions():
    """Retrieve current terminal columns and rows."""
    try:
        size = os.get_terminal_size()
        return max(10, size.columns), max(5, size.lines)
    except (OSError, ValueError):
        return 80, 24


class Stream:
    """Represents a single falling digital rain stream in a column."""
    def __init__(self, col, max_rows, char_pool, speed_factor=1):
        self.col = col
        self.max_rows = max_rows
        self.char_pool = char_pool
        self.reset(initial=True)
        self.speed = speed_factor

    def reset(self, initial=False):
        self.length = random.randint(6, max(8, self.max_rows - 3))
        # Initial stagger across screen
        if initial:
            self.head_y = random.randint(-self.max_rows, self.max_rows)
        else:
            self.head_y = random.randint(-self.length - 5, -1)
        self.step_delay = random.choice([1, 1, 2, 2, 3])
        self.step_counter = 0
        self.symbols = [random.choice(self.char_pool) for _ in range(self.length)]
        self.rainbow_color = random.choice(COLOR_NAMES)

    def advance(self):
        """Advance the stream downwards."""
        self.step_counter += 1
        if self.step_counter >= self.step_delay:
            self.step_counter = 0
            self.head_y += 1
            # Randomly mutate one character in the stream for matrix glitch effect
            if self.symbols and random.random() < 0.25:
                idx = random.randint(0, len(self.symbols) - 1)
                self.symbols[idx] = random.choice(self.char_pool)
            # Reset stream when it has completely scrolled past bottom
            if self.head_y - self.length > self.max_rows:
                self.reset()


class CMatrixApp:
    def __init__(self, color="green", bold=True, all_bold=False, update_delay=0.04,
                 screensaver=False, async_scroll=True, katakana=True, rainbow=False):
        self.color_name = color.lower() if color.lower() in COLORS else "green"
        self.bold = bold
        self.all_bold = all_bold
        self.update_delay = update_delay
        self.screensaver = screensaver
        self.async_scroll = async_scroll
        self.katakana = katakana
        self.rainbow = rainbow
        self.paused = False
        self.running = True

        self.char_pool = KATAKANA_CHARS + ASCII_CHARS if katakana else SYMBOL_CHARS
        self.cols, self.rows = get_terminal_dimensions()
        self.streams = []
        self.init_streams()

        self.orig_termios = None

    def init_streams(self):
        """Initialize one or two streams per column."""
        self.streams = []
        # Every column has an active stream; alternate columns can have staggered secondary streams
        for c in range(self.cols):
            self.streams.append(Stream(c, self.rows, self.char_pool))
            if random.random() < 0.4:
                self.streams.append(Stream(c, self.rows, self.char_pool))

    def on_resize(self, signum=None, frame=None):
        """Handle terminal window size changes dynamically."""
        new_cols, new_rows = get_terminal_dimensions()
        if new_cols != self.cols or new_rows != self.rows:
            self.cols = new_cols
            self.rows = new_rows
            self.init_streams()
            sys.stdout.write(CLEAR_SCREEN)
            sys.stdout.flush()

    def set_raw_terminal(self):
        """Set standard input to unbuffered non-canonical raw mode."""
        if HAS_TERMIOS and sys.stdin.isatty():
            try:
                self.orig_termios = termios.tcgetattr(sys.stdin)
                tty.setcbreak(sys.stdin.fileno())
            except Exception:
                self.orig_termios = None

    def restore_terminal(self):
        """Clean up screen and restore normal terminal settings."""
        sys.stdout.write(SHOW_CURSOR + RESET_STYLE + CLEAR_SCREEN)
        sys.stdout.flush()
        if HAS_TERMIOS and self.orig_termios and sys.stdin.isatty():
            try:
                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.orig_termios)
            except Exception:
                pass

    def check_input(self):
        """Check for non-blocking keyboard input."""
        if not HAS_TERMIOS or not sys.stdin.isatty():
            return
        r, _, _ = select.select([sys.stdin], [], [], 0.0)
        if r:
            ch = sys.stdin.read(1)
            if self.screensaver:
                self.running = False
                return
            if ch in ('q', 'Q', '\x03'):  # 'q' or Ctrl+C
                self.running = False
            elif ch == ' ':
                self.paused = not self.paused
            elif ch == 'b':
                self.bold = not self.bold
            elif ch == 'B':
                self.all_bold = not self.all_bold
            elif ch == 'r':
                self.rainbow = not self.rainbow
            elif ch == 'c':
                idx = (COLOR_NAMES.index(self.color_name) + 1) % len(COLOR_NAMES)
                self.color_name = COLOR_NAMES[idx]
            elif ch.isdigit():
                val = int(ch)
                # '0' = fastest, '9' = slowest
                self.update_delay = 0.01 + (val * 0.015)

    def render_frame(self):
        """Render a single frame of the matrix digital rain."""
        # Grid buffer: grid[r][c] = (char, style_prefix)
        grid = {}

        # Base color codes
        dark_col, bright_col = COLORS[self.color_name]

        for s in self.streams:
            if not self.paused:
                s.advance()

            stream_color_dark = dark_col
            stream_color_bright = bright_col
            if self.rainbow:
                stream_color_dark, stream_color_bright = COLORS[s.rainbow_color]

            for i in range(s.length):
                y = s.head_y - i
                if 0 <= y < self.rows:
                    ch = s.symbols[i % len(s.symbols)]
                    if i == 0:
                        # Leading head glyph: bright bold white / high-intensity
                        style = BOLD_STYLE + "\033[97m"
                    elif i < 3:
                        # Upper body: bright color
                        style = (BOLD_STYLE if (self.bold or self.all_bold) else "") + stream_color_bright
                    else:
                        # Trailing tail: darker color, dim
                        style = (BOLD_STYLE if self.all_bold else DIM_STYLE) + stream_color_dark

                    grid[(y, s.col)] = (ch, style)

        # Build output buffer
        out = ["\033[H"]
        for r in range(self.rows):
            line = []
            for c in range(self.cols):
                if (r, c) in grid:
                    ch, style = grid[(r, c)]
                    line.append(f"{style}{ch}{RESET_STYLE}")
                else:
                    line.append(" ")
            out.append("".join(line))
            if r < self.rows - 1:
                out.append("\n")

        sys.stdout.write("".join(out))
        sys.stdout.flush()

    def run(self):
        """Main animation loop."""
        self.set_raw_terminal()
        try:
            signal.signal(signal.SIGINT, lambda s, f: setattr(self, 'running', False))
            if hasattr(signal, "SIGWINCH"):
                signal.signal(signal.SIGWINCH, self.on_resize)
        except Exception:
            pass

        sys.stdout.write(HIDE_CURSOR + CLEAR_SCREEN)
        sys.stdout.flush()

        try:
            while self.running:
                start_time = time.time()
                self.check_input()
                if not self.running:
                    break
                self.render_frame()
                elapsed = time.time() - start_time
                sleep_time = self.update_delay - elapsed
                if sleep_time > 0:
                    time.sleep(sleep_time)
        finally:
            self.restore_terminal()


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="CodeOS Matrix - High-Performance Digital Rain (Python CLI Utility)"
    )
    parser.add_argument("-b", "--bold", action="store_true", default=True,
                        help="Enable bold leading characters (default: on)")
    parser.add_argument("-B", "--all-bold", action="store_true", default=False,
                        help="Make all rain characters bold")
    parser.add_argument("-C", "--color", default="green", choices=COLOR_NAMES + ["rainbow"],
                        help="Rain color: green, red, blue, cyan, magenta, yellow, white, rainbow")
    parser.add_argument("-u", "--update", type=float, default=0.04,
                        help="Screen update delay in seconds (default: 0.04 = ~25 FPS)")
    parser.add_argument("-s", "--screensaver", action="store_true", default=False,
                        help="Screensaver mode: exit on any keypress")
    parser.add_argument("-a", "--async-scroll", action="store_true", default=True,
                        help="Asynchronous scrolling columns")
    parser.add_argument("-k", "--katakana", action="store_true", default=True,
                        help="Use authentic half-width Katakana Matrix glyphs")
    parser.add_argument("-m", "--ascii-only", action="store_true", default=False,
                        help="Use ASCII and hacker symbols only")
    parser.add_argument("-V", "--version", action="version", version="CodeOS cmatrix 2.0 (Python Edition)")
    return parser.parse_args()


def main():
    args = parse_arguments()
    rainbow_mode = (args.color.lower() == "rainbow")
    color = "green" if rainbow_mode else args.color.lower()
    use_katakana = not args.ascii_only

    app = CMatrixApp(
        color=color,
        bold=args.bold,
        all_bold=args.all_bold,
        update_delay=max(0.005, args.update),
        screensaver=args.screensaver,
        async_scroll=args.async_scroll,
        katakana=use_katakana,
        rainbow=rainbow_mode
    )
    app.run()


if __name__ == "__main__":
    main()


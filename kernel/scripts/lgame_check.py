#!/usr/bin/env python3
"""Pixel-assert an LGame title by driving the real shell builtin.

LGame games are kernel console builtins (`pong`, `snake`) that take over the
framebuffer. The Qt6 desktop runs first on a graphical boot and
`qt_desktop_run()` never returns, so the shell is unreachable -- which is why
limine entry 5 ("CodeOS (Shell + Framebuffer)", cmdline `no-desktop`) exists.
This boots that entry, types the builtin over a bidirectional serial socket,
and asserts against real framebuffer pixels.

Note the bare command names: `games <name>` does NOT launch a graphical game.
`games` with no recognised subcommand lists the csl *text* games and returns.

Reuses shot.py's QMP client and PPM decoder rather than duplicating them.

Usage:
    python3 lgame_check.py pong
    python3 lgame_check.py snake --out /tmp/snake.png
    python3 lgame_check.py selftest      # the fixed probe frame
    python3 lgame_check.py all
"""

import argparse
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shot  # noqa: E402  (needs the sys.path tweak above)

ISO = os.path.join(shot.REPO, "kernel", "codeos-1.0.iso")
SER = "/tmp/lgame-ser.sock"
QMP = "/tmp/lgame-qmp.sock"
PPM = "/tmp/lgame.ppm"

# 1280x800 is what -vga std + SeaBIOS EDID actually gives us; the check fails
# loudly rather than silently misjudging if that ever changes.
W, H = 1280, 800


class Serial:
    """The guest's COM1, both directions.

    shot.py uses `-serial file:`, which is write-only from the guest, so the
    shell can be watched but never spoken to. `games` is a builtin, so we need
    to type into it.
    """

    def __init__(self, path):
        self.buf = b""
        self.s = None
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(path)
                self.s = s
                break
            except OSError:
                time.sleep(0.25)
        if self.s is None:
            raise SystemExit(f"could not connect to serial socket {path}")
        self.s.settimeout(0.2)
        self.t = threading.Thread(target=self._pump, daemon=True)
        self.t.start()

    def _pump(self):
        while True:
            try:
                d = self.s.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not d:
                return
            self.buf += d

    def send(self, text):
        self.s.sendall(text.encode())

    def text(self):
        return self.buf.decode("latin-1", "replace")

    def wait_for(self, needle, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if needle in self.text():
                return True
            time.sleep(0.2)
        return False


def read_ppm(path):
    """Return (w, h, bytearray of RGB) for a binary P6 PPM."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{path}: not a binary PPM (starts {data[:2]!r})")
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos] != 0x0A:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    w, h, maxval = fields
    if maxval != 255:
        raise SystemExit(f"{path}: maxval {maxval}")
    rgb = bytearray(data[pos:pos + w * h * 3])
    if len(rgb) != w * h * 3:
        raise SystemExit(f"{path}: truncated ({len(rgb)} of {w * h * 3})")
    return w, h, rgb


def px(rgb, w, x, y):
    o = (y * w + x) * 3
    return (rgb[o], rgb[o + 1], rgb[o + 2])


def near(a, b, tol=6):
    return all(abs(int(p) - int(q)) <= tol for p, q in zip(a, b))


# The colours pong actually asks for, straight from lgame_pong.c.
#
# BG deserves a note. draw_game() calls lgame_clear(lgame_color_hex(0x001122FF)).
# The trailing FF is the BLUE byte, not an alpha byte: lgame_color_hex reads
# r=(hex>>16), g=(hex>>8), b=hex&0xFF, a=(hex>>24), so 0x001122FF is
# r=0x11 g=0x22 b=0xFF a=0x00 -> a is then promoted to 255. The background is
# therefore rgb(17,34,255). Reading it as 0x001122 and expecting rgb(0,17,34)
# is wrong, and it is a mistake this check made and then had to walk back:
# the framebuffer really is plain RGB (shot_probe.py matches the traffic
# lights by RGB value on this same buffer), so there was never a channel swap
# to find.
#
# Every other probe colour is R==B, so none of them could have detected a
# channel-order error. BG is the only asymmetric one, which is exactly why it
# is the one worth asserting.
BG = (0x11, 0x22, 0xFF)     # lgame_color_hex(0x001122FF)
P1 = (0x00, 0xFF, 0xFF)     # lgame_color_hex(0x00FFFF) -- paddle 1
P2 = (0xFF, 0x00, 0xFF)     # lgame_color_hex(0xFF00FF) -- paddle 2
GREY = (0x44, 0x44, 0x44)    # centre dashes
WHITE = (0xFF, 0xFF, 0xFF)   # ball and score text

# Pong's world. lgame_pong_run() calls lgame_init(800, 600, ...), so
# lgame.width/height are 800/600 -- NOT the 1280x800 surface. Every sample
# point below is derived from those numbers and the constants in
# lgame_pong.c, not eyeballed from a screenshot; getting them wrong is what
# made the first run report a dash pixel as "background".
#
#   PADDLE_W=12 PADDLE_H=80 BALL_SIZE=12
#   paddle1  x = PADDLE_W/2 = 6            (6..17)
#   paddle2  x = 800 - PADDLE_W*3/2 = 782  (782..793)
#   paddles  y = 300 - 40 = 260            (260..339), centred and unmoved
#            because this harness types nothing once the game is up
#   dashes   x = 800/2 - 2 = 398, 4x10, every 20 rows -> y 40..49
#   ball     starts dead centre (400,300) but travels, so never sample it
#   score    "0  -  0" at x=340, y=20, scale 2 -> pixels near (340..410,20..52)
WORLD_W, WORLD_H = 800, 600


def check_pong(rgb, w, h):
    """Assert the stable parts of a pong frame.

    The ball moves and the paddles only move while a key is held, so this
    deliberately samples the paddles, the centre dashes and the clear colour
    rather than the ball. A check that sampled the ball would need to race the
    animation and would flake.
    """
    fails = []

    def want(what, x, y, color, tol=6):
        got = px(rgb, w, x, y)
        if not near(got, color, tol):
            fails.append(f"{what} @({x},{y}) = rgb{tuple(got)}, want "
                         f"rgb{color}")

    want("paddle1", 10, 300, P1)
    want("paddle2", 787, 300, P2)
    want("centre dash", 399, 45, GREY)        # x 398..401, y 40..49
    want("background", 200, 300, BG)          # clear of ball/paddle/score
    return (not fails), fails


def check_snake(rgb, w, h):
    fails = []

    def want(what, x, y, color, tol=6):
        got = px(rgb, w, x, y)
        if not near(got, color, tol):
            fails.append(f"{what} @({x},{y}) = rgb{tuple(got)}, want "
                         f"rgb{color}")

    want("background", 200, 300, BG)
    return (not fails), fails


# ── selftest frame ──
# Mirrors lgame_selftest() in lgame.c. 64x64 blocks from the top-left on an
# opaque blue backdrop.
BLOCK = 64
ST_BG = (0x00, 0x00, 0xFF)       # SELFTEST_BG 0xFF0000FF -> blue


def blend_expect(alpha, src, dst):
    """The blend lgame_blend_pixel() is supposed to perform.

    out = (src*alpha + dst*(255-alpha)) / 255, per channel, with an opaque
    alpha byte on the result. Written out here rather than hardcoding the
    expected pixels so the check states the *formula*; if the formula is
    wrong, this follows it and the check fails, which is the point.
    """
    inv = 255 - alpha
    return tuple((s * alpha + d * inv) // 255 for s, d in zip(src, dst))


def ramp_alpha(x):
    """The texture's alpha at column x: 255 at x=0 down to 0 at x=63."""
    return 255 - (x * 255) // 63


def check_selftest(rgb, w, h):
    fails = []

    def want(what, x, y, color, tol=3):
        got = px(rgb, w, x, y)
        if not near(got, color, tol):
            fails.append(f"{what} @({x},{y}) = rgb{tuple(got)}, want "
                         f"rgb{color}")

    # A: untouched control -- must still be pure backdrop.
    want("A control", 0, 0, ST_BG)
    want("A control", 32, 32, ST_BG)

    # B: the alpha ramp, untinted. The leftmost column is opaque white, so it
    # must be pure white; the rightmost is fully transparent, so the backdrop
    # must show through unchanged. The middle is the blend formula.
    bx, by = BLOCK, 0
    want("B opaque column", bx + 0, by + 32, (255, 255, 255), tol=2)
    want("B transparent column", bx + 63, by + 32, ST_BG, tol=2)
    for probe_x in (16, 32, 48):
        a = ramp_alpha(probe_x)
        want(f"B blend at alpha={a}", bx + probe_x, by + 32,
             blend_expect(a, (255, 255, 255), ST_BG))

    # C: the tinted ramp. The tint multiplies the source's channels, and this
    # tint is green, so r and b scale to 0 and an opaque white texel under it
    # comes out (0,255,0). Note this does NOT show whether the texture was
    # mutated: B is drawn *before* C, so B is already on screen and an in-place
    # mutation cannot reach it. G, below, is the mutation probe.
    cx = BLOCK * 2
    want("C tint applied (opaque texel under green tint)", cx + 0, 32,
         (0, 255, 0), tol=2)

    # D: a 32x32 region at 2x, so it fills 64x64. Its leftmost column is the
    # texture's opaque column and must be white. D and E are drawn after C, so
    # under the old in-place tint they fail as a side effect of reading an
    # already-corrupted texture -- that is expected, and is why G exists.
    want("D scaled region opaque", BLOCK * 3 + 0, 32, (255, 255, 255), tol=2)

    # E: the whole texture flipped in both axes. There is no region-flipped
    # entry point -- lgame_draw_texture_flipped() takes the full texture plus
    # two flags -- so this is the complete 64x64 ramp mirrored and the opaque
    # column that was on the left is now on the right. That is what separates
    # a real flip from a plain copy: a non-flipped blit fails both edges.
    ex = BLOCK * 4
    want("E flip: right edge opaque", ex + 63, 32, (255, 255, 255), tol=2)
    want("E flip: left edge transparent", ex + 0, 32, ST_BG, tol=2)

    # F: text at y=-8 must be clipped to the screen, not written out of
    # bounds. The glyph is 16 rows tall starting at y=-8, so rows 0..7 DO
    # land on screen by design and rows -8..-1 must not appear anywhere --
    # under the old unsigned guard those rows were indexed at a
    # multi-gigabyte offset into the framebuffer. What is observable from a
    # screendump is that the write stayed in bounds: the surface just past
    # the glyph, and the far corner, are still exactly backdrop. If a row had
    # wrapped, it would have landed somewhere else in the buffer and shown up
    # as corruption rather than as this text.
    want("F: surface past the clipped text is untouched", 960, 4, ST_BG, tol=2)
    want("F: top-right corner is untouched", 1270, 0, ST_BG, tol=2)
    want("F: below the clipped text is untouched", 900, 20, ST_BG, tol=2)

    # G: the texture drawn untinted AGAIN, after the tinted draw at C, must be
    # identical to B. This is the mutation probe, and the only assertion that
    # tests the claim directly rather than as a side effect: under the old
    # in-place tint, a *draw* call permanently altered tex->pixels, so G came
    # out green and there was no way back to the original short of recreating
    # the texture. Compared column by column against B so the failure says
    # which alpha first diverged rather than just "some pixel is wrong".
    gx = BLOCK * 6
    for probe_x in (0, 16, 32, 48, 63):
        b = px(rgb, w, bx + probe_x, by + 32)
        g = px(rgb, w, gx + probe_x, by + 32)
        if not near(b, g, tol=1):
            fails.append(f"G untinted re-draw @({gx + probe_x},32) = "
                         f"rgb{tuple(g)}, but block B at the same column is "
                         f"rgb{tuple(b)} -- the tinted draw mutated the texture")
    want("G untinted re-draw is opaque white again", gx + 0, 32,
         (255, 255, 255), tol=2)

    return (not fails), fails


CHECKS = {"pong": check_pong, "snake": check_snake,
          "selftest": check_selftest}


def qemu_argv(mem, boot_entry):
    return [
        "qemu-system-x86_64", "-machine", "q35", "-m", mem, "-smp", "2",
        "-vga", "std",
        "-display", "none",
        "-usb", "-device", "usb-tablet",
        "-boot", "order=d", "-cdrom", ISO,
        "-serial", f"unix:{SER},server,nowait",
        "-qmp", f"unix:{QMP},server,nowait",
        "-nic", "user",
        "-no-reboot",
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game", choices=sorted(CHECKS) + ["all"],
                    help="'all' boots once per title")
    ap.add_argument("--out", default=None)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot-entry", type=int, default=5, metavar="N",
                    help="limine entry N; 5 is 'CodeOS (Shell + Framebuffer)'")
    ap.add_argument("--boot-timeout", type=float, default=90.0)
    ap.add_argument("--capture-timeout", type=float, default=30.0)
    ap.add_argument("--dump-serial", default="/tmp/lgame-ser.log")
    args = ap.parse_args()

    if not os.path.exists(ISO):
        raise SystemExit(f"{ISO} not built -- run: make -C kernel codeos-1.0.iso")

    for p in (SER, QMP, PPM):
        if os.path.exists(p):
            os.unlink(p)

    out = args.out or f"/tmp/lgame-{args.game}.png"
    proc = subprocess.Popen(qemu_argv(args.mem, args.boot_entry),
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        # Same 2s wait shot.py needs: -qmp unix:... bind()s during option
        # parsing but does not serve until the monitor thread runs, and a
        # client that connected during the backlog window is never accepted.
        time.sleep(2.0)
        ser = Serial(SER)
        q = shot.Qmp(QMP)

        # limine menu is up for `timeout:` (5s) -- see limine.conf.
        time.sleep(2.0)
        for _ in range(args.boot_entry - 1):
            q.key("down")
            time.sleep(0.2)
        time.sleep(0.4)
        q.key("ret")

        # AGENTS.md: two boots of one ISO produced byte-identical serial logs
        # and screenshots that disagreed completely, and the menu keys are
        # timed, not deterministic. So never trust that the entry we asked for
        # is the entry that ran -- read it back off the wire.
        got = ser.wait_for("boot: no-desktop", args.boot_timeout)
        if not got:
            with open(args.dump_serial, "w") as f:
                f.write(ser.text())
            shot.ppm_to_png(PPM, out) if os.path.exists(PPM) else None
            raise SystemExit(
                f"the shell+framebuffer entry did not boot (no "
                f"'boot: no-desktop' within {args.boot_timeout}s). The limine "
                f"menu keys are timed -- if they landed outside the 5s window "
                f"the default entry booted instead. Serial: {args.dump_serial}")
        print("boot: shell+framebuffer entry confirmed from the serial log")

        if not ser.wait_for("root#", args.boot_timeout):
            with open(args.dump_serial, "w") as f:
                f.write(ser.text())
            raise SystemExit(f"no shell prompt. Serial: {args.dump_serial}")

        # NOT `games <name>`: `games` with no recognised subcommand just prints
        # a directory listing of the six csl text games (guess, mines, hangman,
        # rps, math, reflex) and returns. The graphical titles are separate
        # top-level builtins -- see the `strcmp(cmd, "pong")` arm in
        # kernel/kernel/shell.c.
        if args.game == "selftest":
            ser.send("lgame-selftest\n")
            marker = "lgame: selftest frame drawn"
        else:
            ser.send(f"{args.game}\n")
            marker = "lgame: fullscreen mode"

        # Prove the game is actually up before judging pixels, for the same
        # reason the boot entry is read back off the wire: a screen full of
        # the shell/bootsplash would otherwise be reported as a game frame
        # that merely has the wrong colours.
        if not ser.wait_for(marker, 20.0):
            with open(args.dump_serial, "w") as f:
                f.write(ser.text())
            raise SystemExit(
                f"`{args.game}` never reached its draw path (no {marker!r} "
                f"line). Serial: {args.dump_serial}")
        print(f"lgame: confirmed {marker!r} on the serial log")

        # Double-buffered, and the game is mid-animation, so poll for a frame
        # that satisfies every assertion rather than trusting one dump.
        deadline = time.time() + args.capture_timeout
        tries, ok, fails = 0, False, []
        while time.time() < deadline:
            tries += 1
            q.screendump(PPM)
            time.sleep(0.5)
            if not os.path.exists(PPM):
                continue
            w, h, rgb = read_ppm(PPM)
            if w != W or h != H:
                raise SystemExit(f"frame is {w}x{h}, expected {W}x{H}")
            ok, fails = CHECKS[args.game](rgb, w, h)
            if ok:
                break
        shot.ppm_to_png(PPM, out)
    finally:
        with open(args.dump_serial, "w") as f:
            try:
                f.write(ser.text())
            except NameError:
                f.write("")
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    print(f"{out}  {W}x{H}  ({tries} capture(s))")
    print(f"serial: {args.dump_serial}")
    if ok:
        print(f"PASS {args.game}: frame satisfied every pixel assertion")
        return 0
    print(f"FAIL {args.game}: {len(fails)} assertion(s) failed")
    for f in fails:
        print(f"  - {f}")
    return 1


if __name__ == "__main__":
    # 'all' is a fresh QEMU per title, not one boot driving all of them: a
    # game takes over the framebuffer and only leaves it on ESC, so a second
    # title in the same boot would be asserting against the first one's frame
    # unless every run also typed ESC. Separate boots are the honest version.
    if len(sys.argv) > 1 and sys.argv[1] == "all":
        rc = 0
        for i, name in enumerate(sorted(CHECKS)):
            if i:
                print()
            sys.argv[1] = name
            rc |= main()
        sys.exit(rc)
    sys.exit(main())

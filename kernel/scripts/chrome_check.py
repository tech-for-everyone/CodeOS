#!/usr/bin/env python3
"""Assert HyperDE's window chrome is in the macOS arrangement, from real pixels.

The chrome is drawn straight into the framebuffer by
`draw_window_chrome()` in hyperde, so there is nothing to screenshot on
the host and nothing to introspect at runtime -- the only honest oracle is the
framebuffer itself. This boots the ISO, clicks the launcher so windows actually
get composited, captures the frame, and measures where the traffic lights are.

What it asserts, per window band found:

  * the red (close) dot is the LEFTMOST of the three, not the rightmost;
  * the triple starts within 24px of the band's left edge (macOS inset);
  * nothing sits in the 70px at the band's right edge (the COSMIC position);
  * the three dots are red/yellow/green, left to right.

The last one is a real risk from the move: the code walks the circles
left-to-right now, so a sign slip paints close in the green slot.

    ./kernel/scripts/chrome_check.py            # boot, click, assert
    ./kernel/scripts/chrome_check.py --png f.png   # re-check a saved frame
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_probe import at, load                                    # noqa: E402

# Geometry the painter must produce, in hyperde's own constants.
WIN_SH = 6            # shadow inset
WIN_TB = 30           # title band height
DOT_D = 12            # traffic-light diameter
PITCH = 20            # centre-to-centre spacing
LEFT_INSET = 14       # band-left to close-centre
GREY = 0x5A5A5E       # unfocused dot colour
RGB = {0xFF5F57: "close", 0xFFBD2E: "min", 0x28C840: "max"}

# fill_circle(cx, cy, 6) covers cx-6..cx+6, so a "12px" dot occupies 13
# pixels and consecutive dots are separated by PITCH-(DOT_D+1) = 7 clear ones.
DOT_W = DOT_D + 1
DOT_GAP = PITCH - DOT_W


def dot_rows(rows, w, h):
    """x positions of dot-coloured pixels, per row, via bytes.find.

    A per-pixel Python loop over 1280x800 is 800M interpreted steps; find()
    is C-speed and the pixels are exactly 3-byte RGB so a substring search is
    the right tool rather than a cleverer loop.
    """
    pats = [c.to_bytes(3, "big") for c in (*RGB, GREY)]
    out = []
    for y in range(h):
        r = rows[y]
        xs = set()
        for p in pats:
            i = r.find(p)
            while i >= 0:
                if i % 3 == 0:
                    xs.add(i // 3)
                i = r.find(p, i + 1)
        out.append(sorted(xs))
    return out


def dot_runs(rows, w, h):
    """Locate the traffic-light triples.

    Returns [(band_y, runs), ...] -- one entry per triple, at the row where
    that circle is at its widest.

    Two things make this fiddlier than it looks. First, two windows often
    share a title-band row, so a single row holds six dot runs, not three --
    they have to be grouped, and the only reliable separator is the 7px gap
    *inside* a triple (any wider gap starts a new group). Second, only the
    circle's widest row is exactly DOT_W px across, so that is the row to
    judge the geometry on; every other row of the same circle is a few pixels
    narrower and must not produce a second, differently-sized report.
    """
    found = {}
    for y, xs in enumerate(dot_rows(rows, w, h)):
        if not xs:
            continue
        runs, s, prev = [], xs[0], xs[0]
        for x in xs[1:]:
            if x == prev + 1:
                prev = x
                continue
            runs.append((s, prev))
            s = prev = x
        runs.append((s, prev))

        groups, g = [], [runs[0]]
        for r in runs[1:]:
            if r[0] - g[-1][1] - 1 == DOT_GAP:
                g.append(r)
            else:
                groups.append(g)
                g = [r]
        groups.append(g)

        for grp in groups:
            if len(grp) != 3:
                continue
            if not all(b - a + 1 == DOT_W for a, b in grp):
                continue
            found.setdefault(grp[0][0], (y, grp))
    return [(y, grp) for y, grp in found.values()]


def band_extent(rows, w, y, x_from, x_to):
    """Walk left/right from a dot to find where the glass band stops being the
    band colour.  Approximate by 'first x whose colour differs from the pixel
    just inside the dot row', which for this gradient is stable row to row."""
    def col(x):
        return at(rows, x, y)
    edge = col(x_from - 4)                 # the glass, 4px left of the triple
    lo = x_from
    while lo > 1 and col(lo - 1) == edge:
        lo -= 1
    hi = x_to
    while hi < w - 1 and col(hi + 1) == edge:
        hi += 1
    return lo, hi


def parse_windows(serial_path):
    """Window rects, straight from the compositor's own log.

    Walking the framebuffer to guess where a window's edges are is guesswork
    -- the title text, the app icon and the focus ring all break a run of
    band colour, so any "how far from the edge" derived that way is really a
    measurement of the title. The compositor already reports the true
    geometry (`PRS pos xid=1 'DevStore' @0,28 764x768`), and draw_window_chrome
    is a pure function of that rect, so the expected dot centres can be
    computed exactly and the pixels only have to confirm them.
    """
    import re
    prs = re.compile(r"PRS pos xid=(\d+) '([^']*)' @(-?\d+),(-?\d+) (\d+)x(\d+)")
    # The Qt/lvgl family reports its own geometry when the tiler lays it out.
    # Both families go through draw_window_chrome, so both are worth judging --
    # the Qt one used to dim its lights with alpha instead of going grey, and
    # nothing short of looking at it would have caught that.
    #
    # This must be the `tile SYNC` line, which prints the rect actually applied
    # to the window. `tile RUN` prints sr=, the *screen* rect the tiling was
    # computed from -- reading that as a window rect put the check 8px right and
    # 36px low and reported a correct window as having no lights at all.
    qt = re.compile(r"tile SYNC ws=\d+ id=(\d+) n=\d+ "
                    r"'@(-?\d+),(-?\d+) (\d+)x(\d+)' title='([^']*)'")
    latest = {}
    with open(serial_path, errors="replace") as f:
        for line in f:
            m = prs.search(line)
            if m:
                g = m.groups()
                # Keep the LAST position for a window, not the first. The demo
                # compositor re-places each window several times as it tiles, and
                # an early rect is not where the chrome was drawn -- checking
                # against a stale rect looks for the dots in empty space and
                # reports "no triple" for a window that is plainly correct.
                latest["x11:" + g[0]] = (int(g[2]), int(g[3]), int(g[4]),
                                         int(g[5]), g[1], "x11")
            m = qt.search(line)
            if m:
                g = m.groups()
                # Same last-wins rule as PRS pos: the tiler re-places windows.
                latest["qt:" + g[0]] = (int(g[1]), int(g[2]), int(g[3]),
                                        int(g[4]), g[5], "qt")
    return list(latest.values())


def dot_at(rows, w, y, cx, half=DOT_D // 2):
    """The colour of the dot centred on (cx, y), or None if there is no dot.

    Sampled at the centre and at both ends of the run so a partial arc cannot
    pass as a full dot.  half is 6, not 7: fill_circle's r=6 spans cx-6..cx+6,
    which is 13 pixels, so sampling at cx-7 would step off the dot and report
    "no dot" for a dot that is plainly there.
    """
    if not (0 <= cx < w):
        return None
    samples = [at(rows, cx, y)]
    for dx in (-half, half):
        if 0 <= cx + dx < w:
            samples.append(at(rows, cx + dx, y))
    if not all(s == samples[0] for s in samples):
        return None
    # Only the palette's three vivid colours and the unfocused grey count as a
    # dot. Without this, a uniform stretch of title-band glass passes the
    # "are the three samples equal" test and comes back as if a dot were
    # there -- which then let the title assertion run against a window whose
    # lights are still on the other side of the band.
    return samples[0] if samples[0] in RGB or samples[0] == GREY else None


def title_start(rows, w, y0, y1, x_from, x_to, bright=0xA0):
    """First column in the band holding a bright (focused-title) pixel, or None.

    Only a focused window draws its title in pal().text; an unfocused one uses
    pal().sub, which is dimmer than this threshold, so this returns None for
    unfocused windows -- exactly the windows whose title position we cannot
    measure, and the ones the caller must therefore skip.
    """
    for x in range(x_from, x_to):
        for y in range(y0, y1):
            c = at(rows, x, y)
            if min((c >> 16) & 255, (c >> 8) & 255, c & 255) > bright:
                return x
    return None


def check(path, serial=None, verbose=True, family=None):
    w, h, rows = load(path)
    if not serial:
        print(f"FAIL {path}: no serial log, so the window rects are unknown. "
              f"Chrome position cannot be judged without the compositor's own "
              f"geometry -- pass --serial")
        return 1

    # Every window the compositor reported, and the subset this run judges.
    # Occlusion is tested against the FULL set even when a family filter is in
    # force: the thing covering a Qt app window's lights is an X11 window, so
    # filtering first would hide the very evidence that explains the failure.
    all_wins = [x for x in parse_windows(serial) if x[1] + x[3] > 28 and x[0] < w]
    wins = [x for x in all_wins if not family or x[5] == family]
    if not wins:
        what = f" of family {family!r}" if family else ""
        if verbose:
            print(f"FAIL {path}: no windows{what} reported in {serial}")
        return 1

    bad = judged = occluded = 0
    for x, y, ww, hh, title, family in wins:
        if ww < PITCH * 3 or x + ww > w:
            continue                        # too narrow, or off the right edge
        # draw_window_chrome's own arithmetic, both arrangements.
        band_y = y + WIN_SH + (WIN_TB - 12) // 2 + 6      # circle centre row
        macos = x + WIN_SH + LEFT_INSET                   # close, then +20/+40
        cosmic = x + ww - WIN_SH - 18                     # close, then -20/-40
        if not 0 <= band_y < h:
            continue                        # band scrolled off the top

        # Is another window's title band sitting on top of this one's lights?
        #
        # render_windows draws the lvgl/Qt stack first and the X11 stack
        # second, on purpose ("penrose windows composite above the Qt stack"),
        # and a title band is an opaque repaint of the whole band rect. So when
        # a Qt app window and a maximized penrose window have overlapping title
        # bands, the later paint erases the earlier lights and no amount of
        # retrying will make them appear. Reporting that as a layout failure
        # would be wrong, and skipping it silently would be worse -- a skipped
        # check is exactly how a regression gate starts passing for the wrong
        # reason -- so it is counted separately and always printed.
        cover = None
        for ox, oy, oww, ohh, otitle, ofam in all_wins:
            if (ox, oy) == (x, y):
                continue
            bx0, bx1 = ox + WIN_SH, ox + oww - WIN_SH - 1
            by0, by1 = oy + WIN_SH, oy + WIN_SH + WIN_TB
            if by0 <= band_y <= by1 and all(bx0 <= macos + PITCH * i <= bx1
                                            for i in range(3)):
                cover = otitle or ofam
                break
        if cover:
            occluded += 1
            if verbose:
                print(f"  [skip] {family}:{title!r} @({x},{y}) lights at "
                      f"x={macos},{macos + 20},{macos + 40} are covered by "
                      f"{cover!r}'s title band -- not judged, and not a pass")
            continue

        # Counted only once the window is actually judgeable, so a window that
        # is skipped cannot pad the denominator and turn a failure into a
        # pass by accident.
        judged += 1

        got = [dot_at(rows, w, band_y, macos + PITCH * i) for i in range(3)]
        # Judge on the *names*, not on got: a band-coloured pixel there is just
        # as much a "no dot" as a None, and keying the branch off `got` sent
        # that case down the ordering path and reported a confusing
        # "order is [None, None, None]".
        names = [RGB.get(c, "grey" if c == GREY else None) for c in got]
        problems = []
        if None in names:
            old = [dot_at(rows, w, band_y, cosmic - PITCH * i) for i in range(3)]
            oldnames = [RGB.get(c, "grey" if c == GREY else None) for c in old]
            if all(n is not None for n in oldnames):
                problems.append(
                    f"no dot at the macOS position (x={macos}..{macos + 40}) but "
                    f"the COSMIC position (x={cosmic - 40}..{cosmic}) holds "
                    f"{oldnames} -- the controls are still on the right")
            else:
                problems.append(
                    f"no complete triple at either the macOS position "
                    f"(x={macos}..{macos + 40}) or the COSMIC position "
                    f"(x={cosmic - 40}..{cosmic})")
        elif names != ["close", "min", "max"] and "grey" not in names:
            problems.append(f"left-to-right order is {names}, want "
                            f"['close', 'min', 'max'] -- close must be the "
                            f"leftmost light")
        elif "grey" in names and len(set(names)) != 1:
            problems.append(f"unfocused window's dots are not uniformly grey: "
                            f"{names}")

        # Title: macOS puts it hard left, immediately past the zoom light.
        # Only measurable on a focused window (see title_start).
        if got and all(c is not None for c in got) and "grey" not in names:
            ts = title_start(rows, w, y + WIN_SH + 6, band_y + 6,
                             macos + PITCH * 2, min(x + ww, w))
            want = macos + PITCH * 2 + 16
            if ts is None:
                problems.append("no title text found to the right of the "
                                "lights at all")
            elif not (want - 4 <= ts <= want + 4):
                where = ("centred in the band" if ts < macos + PITCH * 2 + 8
                         else "past the title's expected offset")
                problems.append(f"title starts at x={ts}, want x={want} "
                                f"(just past the zoom light) -- it is {where}, "
                                f"which is the old centred layout")

        status = "FAIL" if problems else "ok"
        if problems:
            bad += 1
        if verbose:
            print(f"  [{status}] {family}:{title!r} @({x},{y}) {ww}x{hh}  "
                  f"band y={band_y}  dots at x={macos},{macos + 20},"
                  f"{macos + 40} -> {'/'.join(str(n) for n in names)}")
            for p in problems:
                print(f"         - {p}")

    if not judged:
        if verbose:
            print(f"FAIL {path}: none of the {len(wins)} reported windows were "
                  f"judgeable "
                  + (f"({occluded} occluded by another window's title band)"
                     if occluded else ""))
        return 1
    if bad:
        print(f"FAIL {path}: {bad} of {judged} window(s) not in the macOS "
              f"arrangement"
              + (f"; {occluded} more skipped as occluded" if occluded else ""))
        return 1
    if verbose:
        print(f"PASS {path}: {judged} window(s) with close/min/zoom "
              f"left-aligned at the band inset"
              + (f"; {occluded} more skipped as occluded by another window's "
                 f"title band (unverified, not a pass)" if occluded else ""))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", help="check a frame captured earlier instead of "
                                  "booting")
    ap.add_argument("--serial", default=None,
                    help="serial log from the same run (required: it carries "
                         "the window rects the chrome is drawn from)")
    ap.add_argument("--at", type=float, default=40.0)
    ap.add_argument("--timeout", type=float, default=40.0,
                    help="how long to keep re-capturing waiting for a frame "
                         "that is fully composited")
    ap.add_argument("--click", action="append", default=[])
    ap.add_argument("--key", action="append", default=[],
                    help="key combo in QEMU qcodes, '+' separated, e.g. "
                         "meta_l+ret for Super+Return (launches a Qt app "
                         "window); repeatable")
    ap.add_argument("--family", choices=["x11", "qt"], default=None,
                    help="judge only one window family. Needed because the "
                         "families occlude each other: a tiled Qt window is "
                         "1264px wide and covers the X11 demos, so a single "
                         "run cannot see both sets of chrome. Each family is "
                         "verified in its own run.")
    ap.add_argument("--settle", type=float, default=3.0)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--out", default="/tmp/codeos-chrome.png")
    ap.add_argument("--no-demos", action="store_true",
                    help="boot limine entry 4 ('CodeOS (No Demos)'), which "
                         "suppresses the three self-test windows. They are "
                         "tiled to fill the screen and cover the traffic "
                         "lights, which is what made the Qt family skip.")
    args = ap.parse_args()
    serial = args.serial or args.out.replace(".png", "-serial.log")

    if not args.png:
        return boot_and_check(args)
    return check(args.png, args.serial or args.out.replace(".png", "-serial.log"),
                 family=args.family)


def boot_and_check(args):
    """Boot, then capture until a frame passes -- or the deadline runs out.

    Retrying here rather than trusting one timed dump is not defensive
    padding. Two boots of the same ISO produced byte-identical serial logs
    and screenshots that disagreed completely: one had the window bands, the
    other was bare desktop background. Worse, an intermediate frame existed
    that carried *one* window's chrome and none of the others, so a gate as
    weak as "the frame has some dots in it" accepts a half-composited desktop
    and then reports the missing window as a layout bug. The only honest
    definition of a usable frame is "a frame that passes the check", so the
    check drives the capture.
    """
    import time
    import shot

    serial = args.serial or args.out.replace(".png", "-serial.log")
    if os.path.exists(serial):
        os.unlink(serial)
    proc, q = shot.launch(shot.qemu_argv(mem=args.mem, serial=serial))
    try:
        if args.no_demos:
            # Menu-timed, so confirm from the serial log that the entry we
            # asked for is the one that actually ran. If the keys landed
            # before the menu was up, limine silently boots default_entry and
            # the demos come back -- and a chrome check that then measures
            # demo windows while claiming to have booted without them is
            # exactly the silent-wrong-reason failure this script exists to
            # avoid. So this is a hard failure, not a warning.
            time.sleep(2.5)
            for _ in range(3):          # entry 4 is the 4th, so 3x Down
                q.key("down")
                time.sleep(0.2)
            time.sleep(0.4)
            q.key("ret")
            time.sleep(max(0.0, args.at - 3.5))
            if not os.path.exists(serial) or not os.path.getsize(serial):
                print("FAIL --no-demos: no serial log to confirm the boot "
                      "entry")
                return 1
            with open(serial, errors="replace") as f:
                boot = f.read()
            if "no-demos -- self-test windows suppressed" not in boot:
                print("FAIL --no-demos: the no-demos entry did not run. The "
                      "menu keys most likely landed before limine's menu was "
                      "up, so default_entry booted instead. Retry, or raise "
                      "--boot-entry-at.")
                return 1
            demos = [t for t in ("HyperDE", "DevStore", "OpenWeb")
                     if f"'{t}'" in boot]
            if demos:
                print(f"FAIL --no-demos: demo window(s) {demos} still "
                      f"present despite the flag being set")
                return 1
            print("boot entry confirmed: no-demos, 0 demo windows")
        else:
            time.sleep(args.at)
        for spec in args.click:
            x, _, y = spec.partition(",")
            q.click(x, y)
            time.sleep(args.settle)
        for combo in args.key:
            # 'meta_l+ret' -> Super+Return, which is what launchApp(0) hangs
            # off.  The pointer cannot be used for this: the kernel's USB HID
            # mouse branch hardcodes x=0,y=0 and only reports deltas, so a
            # synthesised click never lands on a widget.  The keyboard works.
            q.cmd("send-key", keys=[{"type": "qcode", "data": c}
                                    for c in combo.split("+")])
            time.sleep(args.settle)

        deadline = time.time() + args.timeout
        tries = 0
        best = None
        while time.time() < deadline:
            tries += 1
            q.screendump(shot.PPM)
            time.sleep(0.4)
            shot.ppm_to_png(shot.PPM, args.out)
            rc = check(args.out, serial, verbose=False, family=args.family)
            if rc == 0:
                print(f"passed on capture {tries}")
                check(args.out, serial, verbose=True, family=args.family)
                return 0
            if best is None:
                best = rc
            time.sleep(0.6)

        print(f"no capture passed in {tries} tries ({args.timeout:.0f}s); "
              f"closest run reported:")
        check(args.out, serial, verbose=True, family=args.family)
        return 1
    finally:
        shot.shutdown(proc)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Boot-verify the `img` builtin: decode a real image off a real disk.

The codec decoders have a host suite (`make codec-check`) that exercises the
decoding logic thoroughly. This checks the three things that suite structurally
cannot:

  1. **The code is in the linked image.** `--gc-sections` silently discards a
     package with no caller -- that is how the LGame texture path and all of
     `jengine` were green and absent at the same time. This asserts against
     `nm codeos-1-kernel.bin` *before* booting, so a passing pixel check can
     never be attributed to code the image does not contain.

  2. **The freestanding build behaves like the host build.** The decoders are
     compiled with `-nostdinc -mno-sse -mno-sse2` in the kernel and with libc on
     the host. Code that compiles in both is not necessarily code that behaves
     the same in both.

  3. **The framebuffer really received the pixels.** `codec_image_t` holds
     0xAARRGGBB and `fb_putpixel` takes 0x00RRGGBB; nothing in the codec's own
     tests touches a framebuffer, so that conversion is entirely unchecked
     without this.

Why the expected colours come from the kernel's own probe line rather than a
host-side screendump: the text console repaints over the top-left corner where
the image is blitted, so a screenshot taken after the command returns shows the
prompt, not the image. `img show` therefore reads the framebuffer back with
`fb_getpixel()` and prints it before anything else can overwrite it. Reading it
back also separates "the writes never landed" from "they landed and were then
overwritten" -- two different bugs that look identical from outside.

Expected values are computed here by decoding the fixture independently in
Python, from the bytes on the fixture file. They are not copied from the
decoder's output, so agreement means something.

Usage:
    python3 img_check.py
    python3 img_check.py --keep-scratch
"""

import argparse
import hashlib
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KERNEL = os.path.join(REPO, "kernel")
ISO = os.path.join(KERNEL, "codeos-1.0.iso")
KERNEL_BIN = os.path.join(KERNEL, "codeos-1-kernel.bin")
ORIG_DISK = os.path.join(KERNEL, "disk.img")
GEN_FIXTURES = os.path.join(REPO, "pkgs/core/codec/tests/gen_fixtures.py")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shot as _shot  # noqa: E402

SER = "/tmp/img-check-ser.sock"
QMP = "/tmp/img-check-qmp.sock"
SERIAL_LOG = "/tmp/img-check-ser.log"

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# Every wording `img` uses when it will not do the thing asked of it. Matched
# against the whole segment so a refusal is never mistaken for a result.
REFUSAL = re.compile(
    r"no such file|not found|no framebuffer|no known format|not decodable"
    r"|unrecognised|\bempty\b|input truncated|internally inconsistent"
    r"|unsupported|too large|out of memory|bad argument|expected format",
    re.IGNORECASE)

# Must match the image the check asks the guest to show. bgr24.bmp is used
# because its four pixels are (red, white, black, green): R != B on two of them
# and no two are alike, so a channel swap cannot hide behind symmetric colours
# and a decoder returning all-garbage cannot pass by matching itself.
FIXTURE = "bgr24.bmp"
GUEST_PATH = "/images/" + FIXTURE


class Serial:
    """COM1 in both directions.

    `-serial file:` is write-only from the guest, so the shell can be watched
    but never spoken to -- and `img` is a builtin, so it has to be typed. Same
    socket arrangement lgame_check.py needs for the same reason.
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
        threading.Thread(target=self._pump, daemon=True).start()

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

    def mark(self):
        return len(self.buf)

    def since(self, mark):
        return self.buf[mark:].decode("latin-1", "replace")

    def wait_for(self, needle, timeout, since=None):
        end = time.time() + timeout
        while time.time() < end:
            text = self.text() if since is None else self.since(since)
            if needle in text:
                return True
            time.sleep(0.2)
        return False


class Qmp:
    """Re-exported from shot.py rather than reimplemented.

    The obvious-looking hand-rolled version is wrong: QEMU's send-key takes a
    qcode *name* ("down", "ret"), not the scancode byte sequence, so sending
    \x1b-prefixed bytes presses escape and nothing else -- the boot then silently
    runs the default entry and every later assertion is measuring the wrong
    thing. shot.Qmp is the implementation the LGame and chrome checks already
    drive successfully.
    """

    def __new__(cls, path):
        return _shot.Qmp(path)


def check_symbols():
    """The decoders must be *in the linked binary*, not merely on the link line.

    This runs before the boot for exactly that reason: if a symbol is missing,
    nothing downstream can be trusted, and a check that reported pixel failures
    would be reporting the wrong thing.
    """
    print("==> linked-binary symbol check")
    if not os.path.exists(KERNEL_BIN):
        raise SystemExit(f"{KERNEL_BIN} not built -- run: make -C kernel all")

    out = subprocess.run(["x86_64-elf-nm", KERNEL_BIN],
                         capture_output=True, text=True, check=True).stdout
    present = {line.split()[-1] for line in out.splitlines() if line.split()}

    # lgame and jengine both shipped green with zero of these present.
    required = ["cmd_img", "fb_getpixel", "codec_png_decode", "codec_bmp_decode",
                "codec_wav_decode", "codec_inflate", "codec_probe",
                "codec_decode_auto"]
    bad = []
    for s in required:
        ok = s in present
        print(f"  {'ok  ' if ok else 'FAIL'} {s}")
        if not ok:
            bad.append(s)
    if bad:
        raise SystemExit(
            f"{', '.join(bad)} absent from the linked kernel. It compiled and "
            "linked, then --gc-sections discarded it because nothing calls it. "
            "That is the lgame/jengine trap: fix the caller, not the build.")


def expected_from_bmp(path):
    """Decode the 24-bit BMP independently and return (w, h, {(x, y): (r,g,b)}).

    Written against the BMP spec rather than against the decoder, so agreement
    between this and the guest is evidence rather than tautology. The boot
    fixture is uncompressed 24-bit bottom-up, which is the only variant this
    needs; anything else raises, loudly, rather than returning a wrong answer
    that a broken decoder would then agree with.
    """
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise SystemExit(f"{path}: not a BMP")
    off = struct.unpack_from("<I", d, 10)[0]
    hdr_size = struct.unpack_from("<I", d, 14)[0]
    w, h, planes, bpp, compression = struct.unpack_from("<iiHHI", d, 18)
    if (hdr_size, planes, bpp, compression) != (40, 1, 24, 0):
        raise SystemExit(f"{path}: unexpected BMP variant "
                         f"(hdr={hdr_size} planes={planes} bpp={bpp} "
                         f"compression={compression}); this reader only handles "
                         "uncompressed 24-bit and must not guess at others")

    bottom_up = h > 0
    rows = abs(h)
    stride = (w * 3 + 3) // 4 * 4          # rows are padded to 4 bytes
    need = off + stride * rows
    if need > len(d):
        raise SystemExit(f"{path}: truncated -- needs {need} bytes, has {len(d)}")

    px = {}
    for row in range(rows):
        y = (rows - 1 - row) if bottom_up else row
        base = off + row * stride
        for x in range(w):
            b, g, r = d[base + x * 3:base + x * 3 + 3]   # BMP stores BGR
            px[(x, y)] = (r, g, b)
    return w, rows, px


def build_scratch_disk(workdir):
    """A copy of disk.img with the fixture written into it.

    Never modifies kernel/disk.img. The splice is done with debugfs rather than
    by mounting, because there is no root in this environment and no loop
    device: extract the ext2 partition (it starts at sector 2048, per AGENTS.md),
    write into it, put it back.
    """
    print("==> building a scratch disk with the fixture")
    before = hashlib.md5(open(ORIG_DISK, "rb").read()).hexdigest()

    scratch = os.path.join(workdir, "disk-codec.img")
    shutil.copyfile(ORIG_DISK, scratch)

    fx = os.path.join(workdir, "fx")
    subprocess.run([sys.executable, GEN_FIXTURES, fx],
                   check=True, stdout=subprocess.DEVNULL)
    host_fixture = os.path.join(fx, FIXTURE)

    part = os.path.join(workdir, "part.img")
    with open(scratch, "rb") as f:
        f.seek(2048 * 512)
        partdata = f.read()
    # debugfs needs the partition as a filesystem, not as a whole disk: pointed
    # at disk.img it reports "Bad magic number in super-block" because the
    # ext2 superblock sits at 0x438 *within* the partition, not the image. It
    # also exits 0 when it says that, so the writes have to be verified
    # afterwards rather than trusted.
    with open(part, "wb") as f:
        f.write(partdata)

    for cmd in (["mkdir", "/images"],
                ["write", host_fixture, GUEST_PATH]):
        subprocess.run(["debugfs", "-w", "-R", " ".join(cmd), part],
                       capture_output=True, check=False)

    with open(scratch, "rb") as f:
        head = f.read(2048 * 512)
    original_size = os.path.getsize(scratch)

    with open(scratch, "wb") as f:
        f.write(head)
        f.write(open(part, "rb").read())

    # The image must come out the same size as it went in, and must be verified
    # as a *filesystem* after reassembly rather than as the intermediate part
    # file. An earlier version of this wrote the untouched partition and then
    # the edited one on top of it: 269 MB instead of 129 MB, with the edits
    # sitting past the end of the partition table. The guest then mounted the
    # untouched first copy, found no /images, and `img` reported "no such file"
    # -- which reads exactly like a decoder bug and is not one. Verifying only
    # the intermediate file is what let it through.
    if os.path.getsize(scratch) != original_size:
        raise SystemExit(
            f"scratch image is {os.path.getsize(scratch)} bytes, expected "
            f"{original_size}: the partition was spliced twice")

    spliced = os.path.join(workdir, "spliced.img")
    with open(scratch, "rb") as f:
        f.seek(2048 * 512)
        with open(spliced, "wb") as out:
            out.write(f.read())

    chk = subprocess.run(["debugfs", "-R", "ls -l /images", spliced],
                         capture_output=True, text=True).stdout
    if FIXTURE not in chk:
        raise SystemExit(
            f"{GUEST_PATH} is not in the reassembled scratch image:\n{chk}\n"
            "(present in the intermediate part file but not in the final "
            "image -- the splice is wrong, not the fixture)")
    print(f"  ok   {GUEST_PATH} present in the reassembled scratch image")

    after = hashlib.md5(open(ORIG_DISK, "rb").read()).hexdigest()
    if before != after:
        raise SystemExit("kernel/disk.img changed during the test -- aborting")
    print(f"  ok   kernel/disk.img unchanged (md5 {before[:12]})")
    return scratch


def parse_probe(line):
    """Parse `img: probe x,y=r,g,b ...` into {(x, y): (r, g, b)}."""
    out = {}
    for m in re.finditer(r"(\d+),(\d+)=(\d+),(\d+),(\d+)", line):
        x, y, r, g, b = (int(g) for g in m.groups())
        out[(x, y)] = (r, g, b)
    return out


def qemu_argv(disk):
    return [
        # `-machine pc`, not `q35`. Not a preference: with q35 the
        # `-drive ...,if=ide` device lands on the ICH9 AHCI controller (MMIO at
        # 0xfebf1000) while the kernel's ATA driver probes the legacy ports at
        # 0x1f0, so the guest never sees a disk. Measured, not assumed:
        #   -machine pc  ->  "block: ATA disk detected (129 MB, LBA )"
        #   -machine q35  ->  "block: no disk detected"
        # Without the disk there is no filesystem, every lookup fails, and the
        # decode check would report a decoder bug that is really a missing
        # block device. kernel/Makefile's run targets still say q35.
        "qemu-system-x86_64", "-machine", "pc", "-m", "4G", "-smp", "2",
        # A framebuffer is required: `img show` refuses without one. -vga std
        # plus SeaBIOS EDID resolves to 1280x800 (see AGENTS.md, LGame notes).
        "-vga", "std", "-display", "none",
        "-usb", "-device", "usb-tablet",
        "-boot", "order=d", "-cdrom", ISO,
        # The ISO *carries* a disk.img but the kernel cannot read a file out of
        # the ISO -- there is no ISO9660 reader and no loop device -- so the
        # host has to attach one. See AGENTS.md.
        "-drive", f"file={disk},format=raw,if=ide",
        "-serial", f"unix:{SER},server,nowait",
        "-qmp", f"unix:{QMP},server,nowait",
        "-nic", "user",
        "-no-reboot",
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=ISO,
                    help="ISO to boot. Override with a scratch build: "
                         "`make iso` regenerates disk.img as a side effect "
                         "(it depends on $(USER_ELF)), and disk.img is not "
                         "supposed to be touched by a test run.")
    ap.add_argument("--boot-timeout", type=float, default=240.0)
    ap.add_argument("--keep-scratch", action="store_true")
    args = ap.parse_args()

    iso = args.iso
    if not os.path.exists(iso):
        raise SystemExit(f"{iso} not built -- run: make -C kernel codeos-1.0.iso")
    globals()["ISO"] = iso

    failures = []
    check_symbols()

    workdir = tempfile.mkdtemp(prefix="img-check-")
    disk = build_scratch_disk(workdir)
    fw, fh, want_px = expected_from_bmp(
        os.path.join(workdir, "fx", FIXTURE))
    print(f"==> fixture is {fw}x{fh}; expected corner colours "
          + ", ".join(f"{p}=rgb{want_px[p]}"
                      for p in [(0, 0), (fw - 1, 0), (0, fh - 1), (fw - 1, fh - 1)]))

    for p in (SER, QMP):
        if os.path.exists(p):
            os.unlink(p)

    print("==> booting limine entry 5 (CodeOS: Shell + Framebuffer)")
    proc = subprocess.Popen(qemu_argv(disk), stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        time.sleep(2.0)
        ser = Serial(SER)
        q = Qmp(QMP)

        # Limine's menu is up for `timeout:` (5s, limine.conf). This is the same
        # sequence lgame_check.py uses and it is timing-sensitive: connecting to
        # -qmp earlier than this, or sleeping longer before the first keypress,
        # puts the presses outside the window and limine boots the default entry
        # without saying so.
        time.sleep(2.0)
        for _ in range(4):
            q.key("down")
            time.sleep(0.2)
        time.sleep(0.4)
        q.key("ret")

        # Confirm the entry actually booted rather than assuming it. These menu
        # keys are timed, not deterministic: outside the 5s window limine boots
        # the default entry, the Qt desktop takes over, and a check that
        # believed it had a shell would be judging nothing. This is the same
        # rule chrome_check.py follows.
        if not ser.wait_for("boot: no-desktop", args.boot_timeout):
            open(SERIAL_LOG, "w").write(ANSI.sub("", ser.text()))
            raise SystemExit(
                "the shell+framebuffer entry did not boot (no 'boot: no-desktop'"
                " line). The limine menu keys are timed; if they landed outside "
                f"the 5s window the default entry booted instead. See {SERIAL_LOG}")
        print("  ok   booted the shell+framebuffer entry")

        if not ser.wait_for("root#", 90.0):
            open(SERIAL_LOG, "w").write(ANSI.sub("", ser.text()))
            raise SystemExit(f"no shell prompt; see {SERIAL_LOG}")
        print("  ok   shell reached")

        # ── `img` with no args: the registry, not a hardcoded list ───────────
        mark = ser.mark()
        ser.send("img\n")
        if not ser.wait_for("limits:", 30.0, since=mark):
            failures.append("`img` printed no registry listing")
        else:
            seg = ANSI.sub("", ser.since(mark))
            for want in ("codec registry", "png", "bmp", "wav", "decodes",
                         "recognised", "65536"):
                if want not in seg:
                    failures.append(f"registry listing missing {want!r}")
            print("  ok   `img` lists the registry, including the size limits")

        # ── the fixture must be visible in the guest's filesystem ──────────────
        # `img` reports "no such file" for a file the host wrote and verified
        # with debugfs, so before blaming the decoder, prove the guest can see
        # the directory. These are the ext2-aware builtins (`cat` reads only
        # the in-memory table and would report absent for a file that is
        # really there), and separating the two here is what turns "img is
        # broken" into the actual failing layer.
        mark = ser.mark()
        ser.send("els /\n")
        if not ser.wait_for("els:", 20.0, since=mark) and \
           not ser.wait_for("root#", 20.0, since=mark):
            failures.append("`els /` produced no output at all")
        else:
            root_list = ANSI.sub("", ser.since(mark))
            if "images" not in root_list:
                failures.append(
                    "the guest's ext2 root does not list `images` -- the fixture "
                    "never reached the filesystem the kernel is reading, so every "
                    "decode assertion below would be measuring a missing file. "
                    f"els / said: {root_list.strip()!r}")
            else:
                print("  ok   the guest's ext2 root lists /images")

        mark = ser.mark()
        ser.send("els /images\n")
        if not ser.wait_for("root#", 20.0, since=mark):
            failures.append("`els /images` produced no output")
        else:
            listing = ANSI.sub("", ser.since(mark))
            if FIXTURE not in listing:
                failures.append(
                    f"/images does not contain {FIXTURE} in the guest: "
                    f"{listing.strip()!r}")
            else:
                print(f"  ok   the guest can list {GUEST_PATH}")

        # ── `img probe`: identify without decoding ───────────────────────────
        mark = ser.mark()
        ser.send(f"img probe {GUEST_PATH}\n")
        if not ser.wait_for("img: ", 30.0, since=mark):
            failures.append(f"`img probe {GUEST_PATH}` printed nothing")
        else:
            seg = ANSI.sub("", ser.since(mark))
            # Assert on the affirmative half of the sentence, never on a
            # substring. The first version of this check looked for "bmp" and
            # passed on `img: /images/bgr24.bmp: no such file` -- the filename
            # contains the format name, so the single failure that mattered most
            # was the one it reported as a success. Require the word the probe
            # actually emits, and treat every refusal as a failure of the
            # fixture reaching the guest rather than of the decoder.
            if REFUSAL.search(seg):
                failures.append(
                    f"`img probe` refused the file: {seg.strip()!r} -- the "
                    "fixture did not reach the guest, so nothing after this "
                    "point would be measuring the decoder")
            elif not re.search(r"bmp\b.*\bdecodable\b", seg):
                failures.append(f"`img probe` did not identify it as a "
                                f"decodable bmp: {seg.strip()!r}")
            else:
                print("  ok   `img probe` identifies it as a decodable bmp")

        # ── `img show`: decode and land in the framebuffer ───────────────────
        mark = ser.mark()
        ser.send(f"img show {GUEST_PATH}\n")
        if not ser.wait_for("img: probe", 40.0, since=mark):
            seg = ANSI.sub("", ser.since(mark))
            open(SERIAL_LOG, "w").write(ANSI.sub("", ser.text()))
            # Say *why* it stopped. "Never reached the probe line" on its own is
            # ambiguous between a decode refusal and a fixture that never
            # arrived, and those two need completely different fixes.
            why = ("img refused the file: " + seg.strip()) if seg.strip() else \
                  "img printed nothing at all"
            failures.append(
                f"`img show {GUEST_PATH}` never reached the probe line -- {why}. "
                f"See {SERIAL_LOG}")
            got_px = {}
        else:
            seg = ANSI.sub("", ser.since(mark))
            got_px = parse_probe(seg)

            m = re.search(r"img: decoded (\w+) (\d+)x(\d+) from (\d+) bytes", seg)
            if not m:
                failures.append("no `img: decoded <fmt> <w>x<h>` line")
            else:
                fmt, gw, gh, nbytes = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
                print(f"  ok   decoded: {fmt} {gw}x{gh} from {nbytes} bytes")
                if (gw, gh) != (fw, fh):
                    failures.append(
                        f"decoded {gw}x{gh}, fixture is {fw}x{fh} -- the fixture "
                        "and the decoder disagree about the image's size")
                if fmt != "bmp":
                    failures.append(f"decoded as {fmt}, expected bmp")
                if nbytes != os.path.getsize(
                        os.path.join(workdir, "fx", FIXTURE)):
                    failures.append(
                        f"guest read {nbytes} bytes, fixture is "
                        f"{os.path.getsize(os.path.join(workdir, 'fx', FIXTURE))} "
                        "-- the file did not arrive intact")

            if not got_px:
                failures.append("probe line carried no pixels")
            for (x, y), want in want_px.items():
                if (x, y) not in got_px:
                    continue        # only the corners and centre are probed
                got = got_px[(x, y)]
                ok = got == want
                print(f"  {'ok  ' if ok else 'FAIL'} fb({x},{y}) "
                      f"= rgb{got}, expected rgb{want}")
                if not ok:
                    failures.append(
                        f"framebuffer pixel ({x},{y}) is rgb{got}, expected "
                        f"rgb{want} -- decoded colour did not survive the blit")

            # The centre is probed too when it differs from a corner.
            centre = (fw // 2, fh // 2)
            if centre in got_px and centre not in want_px:
                failures.append("probe read a pixel outside the image")

        open(SERIAL_LOG, "w").write(ANSI.sub("", ser.text()))

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        if args.keep_scratch:
            print(f"  ..   scratch kept at {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    if failures:
        print("\nFAILURES:")
        for f in failures:
            print(f"  FAIL {f}")
        print(f"serial: {SERIAL_LOG}")
        return 1
    print("\nimg_check: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

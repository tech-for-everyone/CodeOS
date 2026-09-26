#!/usr/bin/env python3
"""Filesystem test harness for ext2/ext3/codefs.

Why this exists
---------------
The filesystem is the one subsystem where "the kernel says it worked" is worth
very little: a driver can happily return success while leaving the on-disk
image inconsistent, and the damage only shows up later. So the authority here is
the *host*, not the guest. After the guest has run its script we dd the
partition back out of the disk image and let real e2fsprogs judge the result:

    e2fsck -fn   -- structural check, never writes
    debugfs -R   -- inspect the actual tree the kernel produced

That needs no root: the partition is carved out with dd, so e2fsprogs sees the
filesystem at offset 0, which is the only offset it can read. (Neither
dumpe2fs nor e2fsck can read a filesystem embedded at a nonzero offset, which
is exactly why the Makefile's `-E offset=1048576` images are awkward to verify
by hand.)

Usage
-----
    ./fs_test.py --fstype ext3          # build image, boot, exercise, verify
    ./fs_test.py --fstype ext2          # same, for comparison
    ./fs_test.py --fstype ext3 --keep   # leave the image in place afterwards
"""

import argparse
import os
import pty
import re
import select
import shutil
import subprocess
import sys
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(KERNEL_DIR, ".."))
ISO = os.path.join(KERNEL_DIR, "codeos-1-kernel.iso")
KERNEL_BIN = os.path.join(KERNEL_DIR, "codeos-1-kernel.bin")
WORK = "/tmp/codeos-fs-test"

# Matches the Makefile's disk layout: msdos label, single 0x83 partition
# starting at sector 2048, so the filesystem sits at byte offset 1 MiB.
PART_START_SECTOR = 2048
PART_OFFSET = PART_START_SECTOR * 512
DISK_SIZE_M = 64

# The machine type, boot order and disk placement are all dictated by the
# driver's hardware, not by convenience.  Each of these was found by probing
# (see the notes in scripts/fs_test.py's git history); the short version:
#
#  * ata.c probes the legacy PATA ports (0x1F0/0x3F6), master only.  q35 has no
#    PATA controller at all -- only AHCI -- so on qemu -machine q35 an IDE disk
#    is simply invisible ("block: no disk detected").  i440fx, -machine pc, has
#    PATA, so that is what we must use.
#  * The disk therefore has to own the primary master slot.  A CD-ROM does not
#    contend for it: -cdrom lands on the secondary channel, so booting from the
#    ISO and the disk as if=ide,index=0 works at the same time.
#  * -boot order=d is mandatory once a disk is attached.  Without it SeaBIOS
#    prefers the hard disk, iPXE tries to netboot it, and the guest hangs before
#    it ever reaches storage init -- which looks exactly like "disk not
#    detected" and cost a debugging round.
#  * Booting the kernel directly with -kernel does NOT work: the binary has no
#    PVH ELF note, so qemu refuses it ("Error loading uncompressed kernel").
QEMU = ["qemu-system-x86_64", "-machine", "pc", "-m", "1G", "-smp", "2",
        "-vga", "none", "-nographic", "-monitor", "none",
        "-boot", "order=d", "-cdrom", ISO]

PROMPT = b"root# "
BOOT_TIMEOUT = 60
STEP_TIMEOUT = 30
BOOT_RETRIES = 3


def log(msg):
    print(msg, flush=True)


# ───────────────────────────── image construction ─────────────────────────────

def build_image(disk, fstype, populate=True):
    """Create a partitioned disk with a freshly-mkfs'd filesystem inside it."""
    if os.path.exists(disk):
        os.remove(disk)
    subprocess.run(["truncate", "-s", f"{DISK_SIZE_M}M", disk], check=True)
    subprocess.run(["parted", "-s", disk, "mklabel", "msdos"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["parted", "-s", disk, "mkpart", "primary", "ext2",
                    f"{PART_START_SECTOR}s", "100%"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # -d populates from a directory, so no root and no loop mount is needed.
    stage = os.path.join(WORK, "stage")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "etc"), exist_ok=True)
    os.makedirs(os.path.join(stage, "bin"), exist_ok=True)
    if populate:
        with open(os.path.join(stage, "etc", "conf.txt"), "w") as f:
            f.write("alpha content one\n")
        with open(os.path.join(stage, "bin", "tool.sh"), "w") as f:
            f.write("beta content two\n")

    cmd = ["mke2fs", "-q", "-t", fstype, "-F", "-L", "codeosfs",
           "-b", "1024", "-I", "128", "-E", f"offset={PART_OFFSET}"]
    if populate:
        cmd += ["-d", stage]
    cmd.append(disk)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        log("mke2fs failed:\n" + r.stdout + r.stderr)
        return False
    return True


def extract_partition(disk, out):
    """Carve the filesystem out so e2fsprogs can read it at offset 0."""
    size = os.path.getsize(disk) - PART_OFFSET
    with open(disk, "rb") as fi, open(out, "wb") as fo:
        fi.seek(PART_OFFSET)
        left = size
        while left > 0:
            chunk = fi.read(min(1 << 20, left))
            if not chunk:
                break
            fo.write(chunk)
            left -= len(chunk)
    return os.path.getsize(out)


# ───────────────────────────── guest execution ─────────────────────────────

class Guest:
    def __init__(self, disk, logfile):
        self.disk = disk
        self.logfile = logfile
        self.buf = b""
        self.raw = b""

    def boot(self):
        m, s = pty.openpty()
        # The host pty must be raw, otherwise the guest never sees our bytes
        # as individual keystrokes.
        tty.setraw(s)
        argv = QEMU + ["-drive",
                       f"file={self.disk},format=raw,if=ide,index=0"]
        self.proc = subprocess.Popen(argv, stdin=s, stdout=s, stderr=s,
                                     close_fds=True)
        os.close(s)
        self.master = m
        return self.wait_for(PROMPT, BOOT_TIMEOUT, "boot")

    def pump(self, sec):
        dl = time.time() + sec
        while time.time() < dl:
            r, _, _ = select.select([self.master], [], [], 0.2)
            if not r:
                continue
            try:
                d = os.read(self.master, 65536)
            except OSError:
                return
            if not d:
                return
            self.buf += d
            self.raw += d
            self.logfile.write(d)
            self.logfile.flush()

    def wait_for(self, marker, timeout, what):
        dl = time.time() + timeout
        while time.time() < dl:
            self.pump(0.3)
            if marker in self.buf:
                i = self.buf.index(marker)
                self.buf = self.buf[i + len(marker):]
                return True
        log(f"  !! {what}: {marker!r} not seen within {timeout}s")
        return False

    def send(self, data):
        os.write(self.master, data)

    def cmd(self, line, markers, timeout=STEP_TIMEOUT):
        """Type a shell command and wait for each marker in order."""
        self.send(line.encode() + b"\n")
        return self.wait_all(markers, timeout)

    def wait_all(self, markers, timeout):
        for mk in markers:
            if not self.wait_for(mk, timeout, "marker"):
                return False, mk
        return True, None

    def kill(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=10)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass


# ───────────────────────────── host-side verification ─────────────────────────

def fsck(part):
    """Run a read-only structural check. Returns (rc, output)."""
    r = subprocess.run(["e2fsck", "-fn", part], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def debugfs(part, request):
    # errors="replace" matters: a corrupted filesystem makes debugfs emit raw
    # block bytes, and a strict UTF-8 decode of those aborts the whole run --
    # losing the report for the failure that actually mattered.
    r = subprocess.run(["debugfs", "-R", request, part],
                       capture_output=True, text=True, errors="replace")
    return r.stdout + r.stderr


# ───────────────────────────── the test itself ─────────────────────────────

def run(fstype, keep):
    os.makedirs(WORK, exist_ok=True)
    disk = os.path.join(WORK, f"disk-{fstype}.img")
    part = os.path.join(WORK, f"part-{fstype}.img")
    serial = os.path.join(WORK, f"serial-{fstype}.log")
    report = os.path.join(WORK, f"report-{fstype}.txt")

    results = []
    lines = []

    def step(ok, label, detail=""):
        results.append(ok)
        lines.append(f"  [{'ok ' if ok else 'FAIL'}] {label}"
                     + (f"  {detail}" if detail and not ok else ""))
        log(lines[-1])

    log(f"=== building {fstype} image ===")
    if not build_image(disk, fstype):
        lines.append("mke2fs FAILED")
        open(report, "w").write("\n".join(lines))
        return False
    step(True, f"built {DISK_SIZE_M}M disk with {fstype}")

    # Confirm the image really is what we think it is, before blaming the kernel.
    rc, out = fsck(extract_partition(disk, part) and part)
    step(rc == 0, "freshly built image passes e2fsck", f"rc={rc}\n{out}")

    log(f"=== booting guest with {fstype} disk ===")
    g = Guest(disk, open(serial, "wb"))
    booted = False
    for attempt in range(1, BOOT_RETRIES + 1):
        if g.boot():
            booted = True
            break
        log(f"  boot attempt {attempt} failed; retrying")
        g.kill()
        time.sleep(2)
    step(booted, "guest booted to shell prompt")
    if not booted:
        open(report, "w").write("\n".join(lines))
        return False

    # The partition is auto-mounted at boot (main.c: codefs, then ext2).
    # NB: the shell's own ls/cat/mkdir go through fs.c, which is the in-memory
    # initramfs node table and never touches the disk driver, so they prove
    # nothing about ext2. Everything below uses ext2-native entry points.
    ok, miss = g.cmd("els /", [b"/"])
    step(ok, "els lists the mounted ext2 root", f"missing {miss!r}")

    ok, miss = g.cmd("ecat /etc/conf.txt", [b"alpha content one"])
    step(ok, "ecat reads a file mke2fs populated", f"missing {miss!r}")

    ok, miss = g.cmd("fstest", [b"FSTEST RESULT"])
    step(ok, "fstest ran to completion", f"missing {miss!r}")

    # Parse the per-check lines out of the captured serial output.
    checks = re.findall(rb"FSTEST (ok|FAIL) (\S+)", g.raw)
    named = [(s.decode(), n.decode()) for s, n in checks]
    failed = [n for s, n in named if s == "FAIL"]
    for state, name in named:
        lines.append(f"       fstest {state:4s} {name}")
    step(bool(named) and not failed,
         f"all {len(named)} ext2 driver checks passed",
         "failed: " + ", ".join(failed) if failed else "no checks reported")

    # Flush everything before we pull the plug, so what we verify is what the
    # kernel committed rather than whatever happened to be in its buffers.
    g.cmd("sync", [PROMPT])
    g.pump(1)
    g.kill()

    log("=== verifying the image from the host ===")
    extract_partition(disk, part)
    rc, out = fsck(part)
    step(rc == 0, "e2fsck clean after guest writes", f"rc={rc}\n{out}")

    ls = debugfs(part, "ls -l /")
    step("fstest_small" in ls, "kernel created /fstest_small on disk", ls)
    step("fstest_big" in ls, "kernel created /fstest_big on disk", ls)
    ls2 = debugfs(part, "ls -l /fstest_dir")
    step("File not found" in ls2, "kernel removed /fstest_dir again", ls2)

    st = debugfs(part, "stat /fstest_big")
    m = re.search(r"Size:\s+(\d+)", st)
    step(bool(m) and int(m.group(1)) == 20480,
         "big file has the right size on disk", st)

    cat = debugfs(part, "cat /etc/conf.txt")
    step("alpha content one" in cat,
         "pre-populated file still intact", cat)

    panic = b"OWPANIC" in g.raw
    pf = b"!!! PF at" in g.raw
    step(not panic, "no OWPANIC in serial log")
    step(not pf, "no page fault in serial log")

    allok = all(results)
    lines.insert(0, f"VERDICT: {'ALL STEPS PASSED' if allok else 'FAILURES PRESENT'}"
                    f"  ({fstype})")
    open(report, "w").write("\n".join(lines) + "\n")
    log("")
    log(f"VERDICT: {'ALL STEPS PASSED' if allok else 'FAILURES PRESENT'} ({fstype})")
    log(f"report: {report}")

    if not keep:
        for f in (disk, part):
            try:
                os.remove(f)
            except OSError:
                pass
    return allok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fstype", default="ext3",
                    choices=["ext2", "ext3", "ext4"])
    ap.add_argument("--keep", action="store_true",
                    help="keep the disk image for inspection")
    args = ap.parse_args()
    if not os.path.exists(ISO):
        log(f"missing {ISO} -- build it with: make -C {KERNEL_DIR} codeos-1-kernel.iso")
        return 2
    return 0 if run(args.fstype, args.keep) else 1


if __name__ == "__main__":
    sys.exit(main())

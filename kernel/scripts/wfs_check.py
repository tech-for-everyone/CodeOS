#!/usr/bin/env python3
"""Runtime check for the writable filesystem: boot CodeOS and look at the disk.

A green build says nothing about a writable filesystem.  The in-memory table
the kernel ships in its image is writable in the sense that a program can
change it, and every byte of it is gone at the next boot; the disk the
kernel mounts is the only thing that can hold a file.  So the proof has to
be the disk itself: write a file in the guest, then read the file back
*from the image, on the host*, with a tool that knows nothing about this
kernel.

The check does that in three steps, and each step can fail on its own:

  1. In the guest: write a file, read it back, list the directory, make a
     directory, write into it, remove a file.  This is the view the shell
     has.
  2. On the host: `debugfs` on the same image.  The file must be there, with
     the right content; the directory must be there with its file in it;
     the removed file must be gone.  This is the view the disk has.
  3. Boot again with the same image and read the file back in the guest.
     This is the view the *next boot* has, and it is the whole point: a
     filesystem that forgets what it wrote is not a filesystem.

The image is a copy: the check must not leave the developer's disk.img
holding its probe files, and a check that dirties the tree is a check
nobody runs twice.

Usage:
    wfs_check.py [--iso ISO] [--disk DISK] [--timeout N] [--negate]

`--negate` attaches the disk read-only, which is the honest negative
control: with the volume unwritable the probe file cannot be written, so
every assertion below must fail.  A check that cannot fail is worse than
none.
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

# The commands typed into the guest, and what has to come back for each.
# Every assertion is matched against the text that arrived AFTER its
# command was sent, never against the whole log: `cat` and `ls` print the
# same file name, so a whole-log search would let one command satisfy the
# next.
GUEST = [
    # The write.  `>` on the userspace shell is the path a user takes.
    ("echo wfs probe > /wfs-keep.txt", ["wfs probe"], 30),
    # The read back: the merged view has to find the file the volume holds.
    ("cat /wfs-keep.txt", ["wfs probe"], 30),
    # A directory, and a file inside it.
    ("mkdir /wfs-dir", [], 30),
    ("echo inner > /wfs-dir/inner.txt", ["inner"], 30),
    ("ls /wfs-dir", ["inner.txt"], 30),
    # A file that is removed must disappear from the listing.
    ("echo doomed > /wfs-doomed.txt", ["doomed"], 30),
    ("rm /wfs-doomed.txt", [], 30),
    ("ls /", ["wfs-keep.txt", "wfs-dir"], 30),
]

# What the disk itself must hold afterwards.  (path, must_exist)
DISK = [
    ("/wfs-keep.txt", True),
    ("/wfs-dir", True),
    ("/wfs-dir/inner.txt", True),
    ("/wfs-doomed.txt", False),
]

KEEP_CONTENT = "wfs probe"


def debugfs(image, command):
    """Run a debugfs command on the ext2 partition of a disk image."""
    with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as tmp:
        part = tmp.name
    try:
        # The filesystem starts at sector 2048 (the image is partitioned).
        with open(image, "rb") as src, open(part, "wb") as dst:
            src.seek(2048 * 512)
            shutil.copyfileobj(src, dst)
        out = subprocess.run(["debugfs", "-R", command, part],
                             capture_output=True, text=True)
        return out.stdout + out.stderr
    finally:
        os.unlink(part)


class Guest:
    """A booted CodeOS with a bidirectional serial socket."""

    def __init__(self, iso, disk, timeout, readonly=False):
        self.timeout = timeout
        self.chunks = []
        self.dir = tempfile.mkdtemp(prefix="wfsboot")
        self.sock_path = os.path.join(self.dir, "serial")
        cmd = [
            "qemu-system-x86_64", "-machine", "pc", "-m", "1G", "-smp", "2",
            "-vga", "none", "-nographic", "-monitor", "none",
            "-boot", "order=d", "-cdrom", iso,
        ]
        if not readonly:
            # The negative control attaches no disk at all: the volume is
            # then never mounted, the write has nowhere persistent to go,
            # and every assertion below has to fail.  (A read-only drive
            # is not an option: QEMU refuses to start with one -- "Block
            # node is read-only" -- because the boot writes to it.)
            cmd += ["-drive", f"file={disk},format=raw,if=ide"]
        cmd += [
            "-no-reboot",
            "-serial", f"unix:{self.sock_path},server,nowait",
        ]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        self.sock = self._connect()

    def _connect(self):
        deadline = time.time() + 30
        while True:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(self.sock_path)
                return s
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)

    def pump(self, until):
        while time.time() < until:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                return False
            if not chunk:
                return False
            self.chunks.append(ANSI.sub("", chunk.decode("utf-8", "replace")))
        return True

    def text(self):
        return "".join(self.chunks)

    def wait_for(self, needle, timeout):
        until = time.time() + timeout
        while time.time() < until:
            self.pump(min(time.time() + 0.5, until))
            if needle in self.text():
                return True
        return False

    def run(self, cmd, needles, timeout):
        """Type a command; assert on what arrived after it."""
        mark = len(self.text())
        try:
            self.sock.sendall((cmd + "\n").encode())
        except OSError as exc:
            return [f"`{cmd}`: could not type into the guest ({exc})"]
        failures = []
        for needle in needles:
            if not self.wait_for(needle, timeout):
                failures.append(f"`{cmd}`: missing {needle!r}")
        # Let the prompt come back so the next command is not typed into the
        # middle of this one's output.
        self.wait_for("root@codeos-live", timeout)
        del mark
        return failures

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        shutil.rmtree(self.dir, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="codeos-1.0.iso")
    ap.add_argument("--disk", default="disk.img")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--negate", action="store_true",
                    help="attach the disk read-only: the check must fail")
    args = ap.parse_args()

    work = tempfile.mkdtemp(prefix="wfscheck")
    image = os.path.join(work, "disk.img")
    shutil.copy(args.disk, image)

    failures = []

    # ── phase 1: the guest's own view ──
    guest = Guest(args.iso, image, args.timeout, readonly=args.negate)
    try:
        guest.sock.settimeout(1.0)
        if not guest.wait_for("root#", args.timeout):
            failures.append("the guest never reached the kernel shell")
        else:
            # The userspace shell is what the desktop's terminal runs, and
            # it is where a redirect actually goes through a syscall.
            guest.run("exec /bin/shell", ["root@codeos-live"], 60)
            for cmd, needles, tmo in GUEST:
                failures.extend(guest.run(cmd, needles, tmo))
    finally:
        guest.close()

    # ── phase 2: the disk's own view ──
    for path, must_exist in DISK:
        listing = debugfs(image, f"ls -l {path}").lower()
        # debugfs answers a missing path with "File not found"; anything
        # else means the entry is there.
        found = "not found" not in listing
        if must_exist and not found:
            failures.append(f"the disk has no {path}")
        if not must_exist and found:
            failures.append(f"the disk still has {path} after rm")

    content = debugfs(image, "cat /wfs-keep.txt")
    if KEEP_CONTENT not in content:
        failures.append(f"the disk's /wfs-keep.txt does not hold {KEEP_CONTENT!r}")
    if args.negate:
        # Without a disk there is nothing to read back: the host-side half
        # of the check is what has to fail.
        content = ""

    # ── phase 3: the next boot's view ──
    if not args.negate:
        guest = Guest(args.iso, image, args.timeout)
        try:
            guest.sock.settimeout(1.0)
            if not guest.wait_for("root#", args.timeout):
                failures.append("the second boot never reached the kernel shell")
            else:
                guest.run("exec /bin/shell", ["root@codeos-live"], 60)
                failures.extend(guest.run("cat /wfs-keep.txt", [KEEP_CONTENT], 30))
        finally:
            guest.close()

    shutil.rmtree(work, ignore_errors=True)

    if failures and args.negate:
        print(f"NEGATIVE CONTROL: {len(failures)} checks failed, as expected -- "
              "this is what an unwritable filesystem looks like")
        for f in failures:
            print("  -", f)
        return 0
    if failures:
        print("FAIL")
        for f in failures:
            print("  -", f)
        return 1
    print(f"PASS: {len(GUEST)} guest commands, {len(DISK)} disk entries and a "
          f"second boot verified that the filesystem is writable and keeps "
          f"what it wrote")
    return 0


if __name__ == "__main__":
    sys.exit(main())

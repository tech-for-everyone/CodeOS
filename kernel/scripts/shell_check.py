#!/usr/bin/env python3
"""Runtime check for the shell's pipelines: boot CodeOS and pipe a program.

The shell used to run a pipeline's built-in stages in-process and its
*program* stages with their output going to the terminal, whatever the
pipeline said.  So `bas-hello | cat` printed the line to the terminal and
`cat` got nothing, and `bas-hello | cat > file` left the file empty.  The
stages are now wired together with real pipes.

The proof is the *file*, not the terminal: a pipeline whose output is
redirected to a file has to put the program's output in that file, exactly
once.  The terminal echoes what is typed, so counting lines there would
count the echo as well; a file does not echo.

Every assertion is matched against the text that arrived after its command
was sent, never against the whole log, and the "exactly once" assertions
count occurrences in that window: a pipeline that ran the program twice
would otherwise pass.

Usage:
    shell_check.py [--iso ISO] [--disk DISK] [--timeout N] [--negate]

`--negate` attaches no disk at all.  The files then live only in the
kernel's memory and are gone at the next boot, and the host-side half of
the check has to fail -- which is what shows the check can fail.
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

MARKER = "bas: hello from CodeOS"

# (command, what must appear in the text that came back, how many times)
GUEST = [
    # A program stage piped into a built-in, redirected to a file: the
    # file has to hold the program's output.
    ("bas-hello | cat > /tmp/pipe1.txt", [], 30),
    ("cat /tmp/pipe1.txt", [MARKER], 30),
    # The plain redirect is the control: the same program, no pipeline.
    ("bas-hello > /tmp/pipe2.txt", [], 30),
    ("cat /tmp/pipe2.txt", [MARKER], 30),
    # Three stages.
    ("bas-hello | cat | cat > /tmp/pipe3.txt", [], 30),
    ("cat /tmp/pipe3.txt", [MARKER], 30),
    # A built-in into a built-in still works.
    ("echo pipeline-builtin | cat > /tmp/pipe4.txt", [], 30),
    ("cat /tmp/pipe4.txt", ["pipeline-builtin"], 30),
]

# The files the check expects on the disk, with what they must hold.
DISK = [
    ("/tmp/pipe1.txt", MARKER),
    ("/tmp/pipe2.txt", MARKER),
    ("/tmp/pipe3.txt", MARKER),
    ("/tmp/pipe4.txt", "pipeline-builtin"),
]


def debugfs(image, command):
    with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as tmp:
        part = tmp.name
    try:
        with open(image, "rb") as src, open(part, "wb") as dst:
            src.seek(2048 * 512)
            shutil.copyfileobj(src, dst)
        out = subprocess.run(["debugfs", "-R", command, part],
                             capture_output=True, text=True)
        return out.stdout + out.stderr
    finally:
        os.unlink(part)


class Guest:
    def __init__(self, iso, disk, timeout, with_disk=True):
        self.chunks = []
        self.dir = tempfile.mkdtemp(prefix="shellcheck")
        self.sock_path = os.path.join(self.dir, "serial")
        cmd = [
            "qemu-system-x86_64", "-machine", "pc", "-m", "1G", "-smp", "2",
            "-vga", "none", "-nographic", "-monitor", "none",
            "-boot", "order=d", "-cdrom", iso,
        ]
        if with_disk:
            cmd += ["-drive", f"file={disk},format=raw,if=ide"]
        cmd += ["-no-reboot", "-serial", f"unix:{self.sock_path},server,nowait"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        deadline = time.time() + 30
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(self.sock_path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)
        self.timeout = timeout

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
        mark = len(self.text())
        try:
            self.sock.sendall((cmd + "\n").encode())
        except OSError as exc:
            return [f"`{cmd}`: could not type into the guest ({exc})"], ""
        until = time.time() + timeout
        while time.time() < until:
            self.pump(min(time.time() + 0.5, until))
            if "root@codeos-live" in self.text()[mark:]:
                break
        window = self.text()[mark:]
        failures = []
        for needle in needles:
            count = window.count(needle)
            if count == 0:
                failures.append(f"`{cmd}`: missing {needle!r}")
            elif count > 1:
                failures.append(f"`{cmd}`: {needle!r} appeared {count} times, "
                                "not once")
        return failures, window

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
    ap.add_argument("--negate", action="store_true")
    args = ap.parse_args()

    work = tempfile.mkdtemp(prefix="shellcheck")
    image = os.path.join(work, "disk.img")
    shutil.copy(args.disk, image)

    failures = []
    guest = Guest(args.iso, image, args.timeout, with_disk=not args.negate)
    try:
        guest.sock.settimeout(1.0)
        if not guest.wait_for("root#", args.timeout):
            failures.append("the guest never reached the kernel shell")
        else:
            guest.run("exec /bin/shell", ["root@codeos-live"], 60)
            for cmd, needles, tmo in GUEST:
                f, _ = guest.run(cmd, needles, tmo)
                failures.extend(f)
    finally:
        guest.close()

    # The files have to be on the disk, not just in the kernel's memory.
    for path, content in DISK:
        got = debugfs(image, f"cat {path}")
        if content not in got:
            failures.append(f"the disk's {path} does not hold {content!r}")

    shutil.rmtree(work, ignore_errors=True)

    if failures and args.negate:
        print(f"NEGATIVE CONTROL: {len(failures)} checks failed, as expected -- "
              "this is what a shell without pipelines looks like")
        for f in failures:
            print("  -", f)
        return 0
    if failures:
        print("FAIL")
        for f in failures:
            print("  -", f)
        return 1
    print(f"PASS: {len(GUEST)} shell commands and {len(DISK)} files on the disk "
          f"verified that a pipeline carries a program's output")
    return 0


if __name__ == "__main__":
    sys.exit(main())

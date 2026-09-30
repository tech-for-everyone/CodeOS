#!/usr/bin/env python3
"""Assert which game each builtin name actually launches.

cmd_lgame_game() dispatches on the command name, and it used to do so with
`argv[0][0] == 'p'`. That is invisible from the pong check -- `pong` starts
with 'p', so it launches pong under either implementation -- which is why the
name test is worth its own check. Under the old test, `ps`, `pkg` and
`printenv` all launched pong and took the framebuffer over, and `snake` was
only reachable by accident.

This is a serial-only check: it asserts what each command *prints*, not what
lands on screen. `lgame: fullscreen mode` is the giveaway -- it is printed
immediately before the dispatch, and then never returns while the game runs, so
if it appears the framebuffer is gone and the shell is unreachable.

Usage:
    python3 lgame_dispatch_check.py
"""

import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shot  # noqa: E402  (needs the sys.path tweak above)

ISO = os.path.join(shot.REPO, "kernel", "codeos-1.0.iso")
SER = "/tmp/lgamedis-ser.sock"
QMP = "/tmp/lgamedis-qmp.sock"

# command -> (should it take over the framebuffer, a string unique to that game)
CASES = [
    ("ps",       False, None),
    ("pkg",      False, None),
    ("printenv", False, None),
    ("snake",    True,  "snake"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot-entry", type=int, default=5, metavar="N")
    ap.add_argument("--boot-timeout", type=float, default=90.0)
    ap.add_argument("--dump-serial", default="/tmp/lgamedis-ser.log")
    args = ap.parse_args()

    if not os.path.exists(ISO):
        raise SystemExit(f"{ISO} not built -- run: make -C kernel codeos-1.0.iso")

    for p in (SER, QMP):
        if os.path.exists(p):
            os.unlink(p)

    proc = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-m", args.mem, "-smp", "2",
        "-vga", "std", "-display", "none",
        "-boot", "order=d", "-cdrom", ISO,
        "-serial", f"unix:{SER},server,nowait",
        "-qmp", f"unix:{QMP},server,nowait",
        "-nic", "user",
        "-no-reboot",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    text = ""
    fails = []
    try:
        time.sleep(2.0)
        import socket
        s = None
        end = time.time() + 30
        while time.time() < end:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(SER)
                break
            except OSError:
                s = None
                time.sleep(0.25)
        if s is None:
            raise SystemExit(f"could not connect to {SER}")
        s.settimeout(0.3)
        import threading
        buf = [b""]

        def pump():
            while True:
                try:
                    d = s.recv(4096)
                except socket.timeout:
                    continue
                except OSError:
                    return
                if not d:
                    return
                buf[0] += d
        threading.Thread(target=pump, daemon=True).start()

        def wait_for(needle, timeout, since=0):
            """Wait for `needle` in buf[since:] rather than the whole buffer.

            Anchoring matters here: `root#` is already in the log from the
            prompt that launched the game, so an unanchored wait after ESC
            returns instantly on that stale match and the check reports a
            clean return to the shell while the game is still running. The
            two boot-time waits legitimately search history, so the default
            is 0 and the per-case waits pass a mark.
            """
            e = time.time() + timeout
            while time.time() < e:
                if needle in buf[0][since:].decode("latin-1", "replace"):
                    return True
                time.sleep(0.2)
            return False

        def all_text():
            return buf[0].decode("latin-1", "replace")

        q = shot.Qmp(QMP)
        time.sleep(2.0)
        for _ in range(args.boot_entry - 1):
            q.key("down")
            time.sleep(0.2)
        time.sleep(0.4)
        q.key("ret")

        if not wait_for("boot: no-desktop", args.boot_timeout):
            raise SystemExit(
                f"the shell+framebuffer entry did not boot. The limine menu "
                f"keys are timed against a 5s window, so sending the right "
                f"number of downs is not evidence. Log: {args.dump_serial}")
        if not wait_for("root#", args.boot_timeout):
            raise SystemExit(f"no shell prompt. Log: {args.dump_serial}")
        print(f"boot: shell+framebuffer entry confirmed from the serial log")

        for cmd, takes_over, _ in CASES:
            before = len(all_text())
            s.sendall(f"{cmd}\n".encode())
            time.sleep(3.0)
            chunk = all_text()[before:]

            took_over = "lgame: fullscreen mode" in chunk
            if took_over != takes_over:
                if takes_over:
                    fails.append(f"`{cmd}` did not take over the framebuffer "
                                 f"(no 'lgame: fullscreen mode' in its output)")
                else:
                    fails.append(f"`{cmd}` took over the framebuffer -- "
                                 f"'lgame: fullscreen mode' appeared, so the "
                                 f"dispatch test matched a non-game command")
            else:
                verdict = "launched a game" if takes_over else "stayed in the shell"
                print(f"  {cmd:<10} {verdict}")

            if takes_over:
                # ESC goes out on the SERIAL line, the same console the command
                # was typed at -- not over QMP, which would reach the emulated
                # PS/2 keyboard and prove only that a keyboard can stop a game.
                # The graphical titles are reachable *only* as fullscreen shell
                # builtins (qt_desktop_run() never returns), so on this boot
                # entry the serial line is the user's only input device.
                quitting = len(buf[0])
                s.sendall(b"\x1b")
                if not wait_for("root#", 20.0, since=quitting):
                    fails.append(f"after `{cmd}` the shell prompt never came "
                                 f"back -- the game did not return")
            elif not wait_for("root#", 15.0, since=before):
                fails.append(f"after `{cmd}` the shell prompt never came back")
            # Drain so the next case's `before` is a clean boundary.
            time.sleep(0.5)
    finally:
        try:
            with open(args.dump_serial, "w") as f:
                f.write(all_text())
        except Exception:
            pass
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    print(f"serial: {args.dump_serial}")
    if fails:
        print(f"FAIL dispatch: {len(fails)} problem(s)")
        for f in fails:
            print(f"  - {f}")
        return 1
    print(f"PASS dispatch: every command name reached its own game")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Prove that x86_64 linker discovery finds a linker that really works.

`make` used to fall back to the host `ld` when x86_64-elf-ld was absent.  On
an aarch64 host that linker cannot emit x86_64 ELF at all, so the error only
appeared after compiling every userspace object.  This check compiles a tiny
freestanding x86_64 object, links it with exactly the command the Makefiles
discover, and verifies the ELF machine.  On a cross host it also asserts that
the old bare-`ld` fallback fails, so the test cannot pass for the wrong reason.
"""

import pathlib
import platform
import shlex
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = 'void _start(void) { for (;;) {} }\n'


def fail(message):
    print(f'linker-check: FAIL {message}', file=sys.stderr)
    raise SystemExit(1)


def run(cmd, **kwargs):
    return subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, **kwargs)


def link_with(linker, obj, output):
    cmd = shlex.split(linker) + ['-nostdlib', '-z', 'noexecstack',
                                 '--entry=_start', '-o', str(output), str(obj)]
    return run(cmd)


def main():
    discovery = ROOT / 'scripts' / 'find_x86_64_ld.sh'
    found = run([str(discovery), 'x86_64'])
    if found.returncode != 0:
        fail(f'linker discovery failed: {found.stderr.strip()}')
    linker = found.stdout.strip()
    if not linker:
        fail('linker discovery printed an empty command')

    with tempfile.TemporaryDirectory(prefix='codeos-linker-') as tmp:
        tmp = pathlib.Path(tmp)
        src = tmp / 'start.c'
        obj = tmp / 'start.o'
        exe = tmp / 'start'
        src.write_text(SOURCE)

        compiled = run(['clang', '--target=x86_64-unknown-none', '-ffreestanding',
                        '-fno-pic', '-c', '-o', str(obj), str(src)])
        if compiled.returncode != 0:
            fail(f'cannot compile an x86_64 test object: {compiled.stderr.strip()}')

        linked = link_with(linker, obj, exe)
        if linked.returncode != 0:
            fail(f'{linker} cannot link x86_64 ELF: {linked.stderr.strip()}')
        with exe.open('rb') as stream:
            header = stream.read(20)
        if len(header) < 20 or header[:4] != b'\x7fELF' or header[4] != 2:
            fail(f'{linker} did not produce a 64-bit ELF')
        # ELF64: e_type at 16, e_machine at 18.  Reading e_type here makes the
        # check report 2 (ET_EXEC) instead of 62 (EM_X86_64).
        machine = int.from_bytes(header[18:20], 'little')
        if machine != 62:  # EM_X86_64
            fail(f'{linker} produced ELF machine {machine}, expected 62')

    host = platform.machine().lower()
    if host in ('x86_64', 'amd64'):
        print('linker-check: host is x86_64; bare-ld negative control skipped')
    else:
        with tempfile.TemporaryDirectory(prefix='codeos-linker-') as tmp:
            tmp = pathlib.Path(tmp)
            src = tmp / 'start.c'
            obj = tmp / 'start.o'
            src.write_text(SOURCE)
            compiled = run(['clang', '--target=x86_64-unknown-none', '-ffreestanding',
                            '-fno-pic', '-c', '-o', str(obj), str(src)])
            if compiled.returncode != 0:
                fail(f'cannot compile an x86_64 test object: {compiled.stderr.strip()}')
            naive = link_with('ld', obj, tmp / 'naive')
            if naive.returncode == 0:
                fail('bare host ld linked x86_64 ELF; the negative control is stale')
        print('linker-check: bare host ld rejected x86_64 ELF (expected)')

    print(f'linker-check: {linker} emits x86_64 ELF')
    return 0


if __name__ == '__main__':
    sys.exit(main())

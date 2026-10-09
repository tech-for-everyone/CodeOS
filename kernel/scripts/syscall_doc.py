#!/usr/bin/env python3
"""syscall_doc.py — Parse syscall.h and syscall.c to generate syscall documentation.

Extracts syscall numbers, names, argument signatures, and dispatch cases
from the kernel source. Outputs a formatted reference table.

Usage:
    python3 scripts/syscall_doc.py                    # full documentation
    python3 scripts/syscall_doc.py --compact          # one-line-per-syscall
    python3 scripts/syscall_doc.py --missing          # find gaps in numbering
    python3 scripts/syscall_doc.py --json             # JSON output

Output: syscall reference table with numbers, names, and case locations.
"""

import os
import re
import sys
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.dirname(SCRIPT_DIR)
SYSCALL_H = os.path.join(KERNEL_DIR, "kernel", "syscall.h")
SYSCALL_C = os.path.join(KERNEL_DIR, "kernel", "syscall.c")


def parse_header(path):
    syscalls = {}
    try:
        with open(path, "r") as f:
            content = f.read()
    except (OSError, IOError):
        return syscalls

    define_pat = re.compile(r"#define\s+SYSCALL_(\w+)\s+(\d+)")
    comment_pat = re.compile(r"//\s*(.+)")
    lines = content.split("\n")

    current_comment = ""
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("//"):
            current_comment = stripped[2:].strip()
            continue

        m = define_pat.search(stripped)
        if m:
            name = m.group(1)
            num = int(m.group(2))
            syscalls[num] = {
                "name": f"SYSCALL_{name}",
                "short": name,
                "number": num,
                "comment": current_comment,
                "args": [],
                "return_type": "int64_t",
            }
            current_comment = ""
        elif not stripped.startswith("#") and not stripped.startswith("/*"):
            if stripped:
                current_comment = ""

    return syscalls


def parse_dispatch(path, syscalls):
    try:
        with open(path, "r") as f:
            content = f.read()
    except (OSError, IOError):
        return

    case_pat = re.compile(r"case\s+SYSCALL_(\w+)\s*:")
    lines = content.split("\n")

    for i, line in enumerate(lines):
        m = case_pat.search(line)
        if m:
            name = m.group(1)
            for num, sc in syscalls.items():
                if sc["short"] == name:
                    sc["case_line"] = i + 1
                    sc["case_file"] = os.path.basename(path)
                    break


def find_args(syscalls):
    for num, sc in syscalls.items():
        name = sc["short"]
        arg_map = {
            "WRITE": ["fd", "buf", "len"],
            "READ": ["fd", "buf", "len"],
            "OPEN": ["path", "flags", "mode"],
            "CLOSE": ["fd"],
            "EXIT": ["code"],
            "SLEEP": ["ms"],
            "FORK": [],
            "EXECVE": ["path", "argv", "envp"],
            "GETPID": [],
            "MMAP": ["addr", "len", "prot", "flags", "fd", "offset"],
            "MUNMAP": ["addr", "len"],
            "MALLOC": ["size"],
            "FREE": ["ptr"],
            "GETTID": [],
            "CLONE": ["flags", "stack", "ptid", "ctid", "tls"],
            "WAIT4": ["pid", "status", "options"],
            "KILL": ["pid", "sig"],
            "VM": ["cmd", "arg1", "arg2", "arg3", "arg4"],
            "TIME": ["buf"],
            "GETCWD": ["buf", "size"],
            "CHDIR": ["path"],
            "MKDIR": ["path", "mode"],
            "RMDIR": ["path"],
            "UNLINK": ["path"],
            "RENAME": ["old", "new"],
            "STAT": ["path", "buf"],
            "FSTAT": ["fd", "buf"],
            "LSEEK": ["fd", "offset", "whence"],
            "DUP": ["fd"],
            "DUP2": ["oldfd", "newfd"],
            "PIPE": ["fds"],
            "IOCTL": ["fd", "request", "arg"],
            "FCNTL": ["fd", "cmd", "arg"],
            "SELECT": ["nfds", "readfds", "writefds", "timeout"],
            "POLL": ["fds", "nfds", "timeout"],
            "DIRENT": ["fd", "buf", "count"],
            "GETUID": [],
            "GETGID": [],
            "SETUID": ["uid"],
            "SETGID": ["gid"],
            "GETPPID": [],
            "SETPGID": ["pid", "pgid"],
            "GETPGID": ["pid"],
            "SETSID": [],
            "SIGACTION": ["sig", "act", "oact"],
            "SIGPROCMASK": ["how", "set", "oset"],
            "SIGRETURN": [],
            "ALARM": ["seconds"],
            "NICE": ["inc"],
            "GETRUSAGE": ["who", "usage"],
            "SYSINFO": ["info"],
            "MOUNT": ["source", "target", "fstype", "flags", "data"],
            "UMOUNT": ["target"],
            "SET_HOSTNAME": ["name"],
            "SET_DOMAINNAME": ["name"],
            "GETTIMEOFDAY": ["tv", "tz"],
            "CLOCK_GETTIME": ["clk_id", "ts"],
            "CLOCK_SETTIME": ["clk_id", "ts"],
            "CREATE_CONTAINER": ["flags"],
            "ENTER_NAMESPACE": ["pid", "ns_type"],
            "BINDER_TRANSACTION": ["fd", "data", "len"],
            "BINDER_REPLY": ["fd", "data", "len"],
            "BINDER_FREE_BUF": ["fd", "data", "len"],
            "ASHMEM_CREATE": ["size", "name"],
            "ASHMEM_MMAP": ["fd", "addr", "size", "prot"],
            "ASHMEM_SET_NAME": ["fd", "name"],
            "ASHMEM_GET_NAME": ["fd", "buf", "len"],
            "ZIRCON_IPC": ["cmd", "arg1", "arg2"],
            "X11_CONNECT": ["display"],
            "X11_CREATE_WINDOW": ["x", "y", "w", "h"],
            "X11_MAP_WINDOW": ["win"],
            "X11_DRAW_TEXT": ["win", "x", "y", "text"],
            "X11_DRAW_RECT": ["win", "x", "y", "w", "h"],
            "X11_DRAW_LINE": ["x1", "y1", "x2", "y2"],
            "X11_EVENT_LOOP": ["win", "event_buf", "buf_size"],
            "X11_FLUSH": [],
            "X11_SET_TITLE": ["win", "title"],
            "X11_DESTROY_WINDOW": ["win"],
            "WEB": ["cmd", "arg1", "arg2", "arg3", "arg4"],
            "AI_QUERY": ["prompt", "response_buf", "buf_size"],
            "SOCKET": ["domain", "type", "protocol"],
            "BIND": ["fd", "addr", "addrlen"],
            "LISTEN": ["fd", "backlog"],
            "ACCEPT": ["fd", "addr", "addrlen"],
            "CONNECT": ["fd", "addr", "addrlen"],
            "SEND": ["fd", "buf", "len", "flags"],
            "RECV": ["fd", "buf", "len", "flags"],
            "SENDTO": ["fd", "buf", "len", "flags", "addr", "addrlen"],
            "RECVFROM": ["fd", "buf", "len", "flags", "addr", "addrlen"],
            "SHUTDOWN": ["fd", "how"],
            "GETSOCKNAME": ["fd", "addr", "addrlen"],
            "GETPEERNAME": ["fd", "addr", "addrlen"],
            "SETSOCKOPT": ["fd", "level", "optname", "optval", "optlen"],
            "GETSOCKOPT": ["fd", "level", "optname", "optval", "optlen"],
            "AUDIO_OPEN": ["device", "format", "rate", "channels"],
            "AUDIO_CLOSE": ["handle"],
            "AUDIO_WRITE": ["handle", "buf", "len"],
            "AUDIO_READ": ["handle", "buf", "len"],
            "AUDIO_SET_FORMAT": ["handle", "format"],
            "AUDIO_SET_RATE": ["handle", "rate"],
            "AUDIO_SET_CHANNELS": ["handle", "channels"],
            "AUDIO_GET_POS": ["handle"],
            "AUDIO_GET_MASTER": [],
        }
        if name in arg_map:
            sc["args"] = arg_map[name]


def format_doc(syscalls, compact=False):
    lines = []
    lines.append("CodeOS Syscall Reference")
    lines.append("=" * 70)

    if compact:
        lines.append(f"{'#':>4s}  {'Name':30s}  {'Args':s}")
        lines.append("-" * 70)
        for num in sorted(syscalls.keys()):
            sc = syscalls[num]
            args = ", ".join(sc["args"]) if sc["args"] else "void"
            lines.append(f"{num:4d}  {sc['name']:30s}  ({args})")
    else:
        for num in sorted(syscalls.keys()):
            sc = syscalls[num]
            args = ", ".join(sc["args"]) if sc["args"] else "void"
            lines.append(f"\n  {num:3d}  {sc['name']}")
            lines.append(f"       Args: ({args})")
            if sc.get("comment"):
                lines.append(f"       Note: {sc['comment']}")
            if sc.get("case_line"):
                lines.append(f"       Impl: {sc.get('case_file', 'syscall.c')}:{sc['case_line']}")
            else:
                lines.append(f"       Impl: (not found in dispatch)")

    return "\n".join(lines)


def find_gaps(syscalls):
    if not syscalls:
        return []
    nums = sorted(syscalls.keys())
    max_n = max(nums)
    gaps = []
    for i in range(max_n + 1):
        if i not in nums:
            gaps.append(i)
    return gaps


def main():
    compact = False
    show_missing = False
    output_json = False

    args = sys.argv[1:]
    for a in args:
        if a == "--compact":
            compact = True
        elif a == "--missing":
            show_missing = True
        elif a == "--json":
            output_json = True
        elif a == "--help" or a == "-h":
            print(__doc__)
            sys.exit(0)

    syscalls = parse_header(SYSCALL_H)
    parse_dispatch(SYSCALL_C, syscalls)
    find_args(syscalls)

    if output_json:
        print(json.dumps(syscalls, indent=2))
        return

    if show_missing:
        gaps = find_gaps(syscalls)
        if gaps:
            print(f"Missing syscall numbers ({len(gaps)} gaps):")
            for g in gaps:
                print(f"  {g:3d}")
        else:
            print("No gaps in syscall numbering.")
        print(f"\nHighest defined: {max(syscalls.keys()) if syscalls else 'none'}")
        print(f"Total defined:   {len(syscalls)}")
        return

    print(format_doc(syscalls, compact))
    print(f"\nTotal: {len(syscalls)} syscalls defined")


if __name__ == "__main__":
    main()

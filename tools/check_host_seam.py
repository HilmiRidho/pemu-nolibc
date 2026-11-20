#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Step 1 of the road to a pemu without a host kernel: everything that leaves the emulator for the host kernel goes through
host/pemu_host.h.  This fails when code in src/ or the entry file calls one of these directly again:

    mmap munmap mprotect madvise fork vfork waitpid pthread_create pthread_exit syscall
    open openat close read write pread lseek stat lstat fstat fstatat

(use host_mmap ... host_syscall_e, host_open ... host_fstatat instead).  It also fails on a Linux syscall number by name (SYS_openat ...): src/ names host operations (HOP_openat),
and only host/pemu_hop.h says what number that is on this host (step 5: the Linux ABI layer no longer knows the host's syscall numbers).  Comments and string / character literals are ignored.  Step 2b moved getpid, kill, raise, _exit, mkdir, unlink, rmdir, rename, chdir, getcwd, dup2, pipe2, fcntl; a later step moves more
(signals, stdio, execvp, getenv, ...) behind the header; add their names to FORBIDDEN then.   usage: python3 tools/check_host_seam.py [ROOT]"""
import os
import re
import sys

FORBIDDEN = ("mmap", "munmap", "mprotect", "madvise", "fork", "vfork", "waitpid", "pthread_create", "pthread_exit", "syscall",
             "open", "openat", "close", "read", "write", "pread", "lseek", "stat", "lstat", "fstat", "fstatat",
             "getpid", "kill", "raise", "_exit", "mkdir", "unlink", "rmdir", "rename", "chdir", "getcwd", "dup2", "pipe2", "fcntl")
SYSNR = re.compile(r"\bSYS_[a-z0-9_]+\b")       # a host syscall number by its Linux name: src/ asks for HOP_<name> (host/pemu_hop.h) instead
LITNR = re.compile(r"\b(hs|host_syscall_e?|pemu_call[a-z0-9_]*)\(\s*\d+")   # a bare number as the host operation: it hides a Linux number the same way (use HOP_<name>)
CALL = re.compile(r"(?<![\w>.])(" + "|".join(FORBIDDEN) + r")\s*\(")
DIRS = ("src",)
FILES = ("src/pemu.c",)


def strip(src):
    """Blank out comments and string / char literals, keeping every newline so line numbers stay right."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == "/*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif two == "//":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + "".join("\n" if ch == "\n" else " " for ch in src[i + 1:j - 1]) + c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    paths = [os.path.join(root, f) for f in FILES]
    for d in DIRS:
        for dp, _, fs in os.walk(os.path.join(root, d)):
            paths += [os.path.join(dp, f) for f in fs if f.endswith((".h", ".c"))]
    bad = 0
    for p in sorted(paths):
        if not os.path.isfile(p):
            continue
        with open(p, encoding="utf-8", errors="replace") as fh:
            text = strip(fh.read())
        for ln, line in enumerate(text.split("\n"), 1):
            for m in LITNR.finditer(line):
                print("%s:%d: %s() with a literal number as the host operation - use HOP_<name> (add it to FALLBACK in tools/gen_hop.py if old headers lack it)" % (os.path.relpath(p, root), ln, m.group(1)))
                bad += 1
            for m in SYSNR.finditer(line):
                print("%s:%d: %s names a Linux syscall number - use HOP_%s (host/pemu_hop.h; python3 tools/gen_hop.py)" % (os.path.relpath(p, root), ln, m.group(0), m.group(0)[4:]))
                bad += 1
            for m in CALL.finditer(line):
                print("%s:%d: direct %s() - use host_%s() from host/pemu_host.h" % (os.path.relpath(p, root), ln, m.group(1),
                      "syscall_e" if m.group(1) == "syscall" else {"pthread_create": "thread_create", "pthread_exit": "thread_exit", "vfork": "fork"}.get(m.group(1), m.group(1))))
                bad += 1
    if bad:
        print("host seam: %d direct call(s) bypass host/pemu_host.h" % bad)
        return 1
    print("host seam: no direct host calls outside host/pemu_host.h (%d files)" % len(paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())

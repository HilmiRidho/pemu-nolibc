#!/bin/sh
# The guest's own identity in /proc (x86_64 guest under pemu, x86_64 host, plain gcc + static libc):
#   readlink("/proc/self/exe") named the EMULATOR without a rootfs, and execve("/proc/self/exe") ran the emulator itself with no program
#   (also in a rootfs, where only readlink was translated); /proc/self/cmdline showed the emulator's command line.  Electron / Chromium
#   find their resources through the first and start every child process through the second, so they could not run at all.
#   prctl(PR_SET_SECCOMP) from a guest would have armed an x86_64 filter on the host process.  Now the guest's filter is run by the emulator
#   (proot/seccomp.inc.h, tests/integration/seccomp.sh); here: without no_new_privs the answer is the kernel's EACCES, and the emulator is still alive to say so.
# Skips (exit 0) when the host is not x86_64 or gcc cannot link statically.  usage: tests/integration/selfexe.sh [BUILD_DIR]
cd "$(dirname "$0")/../.."
B=${1:-build}
B=$(cd "$B" && pwd)
[ "$(uname -m)" = x86_64 ] || { echo "selfexe: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/guests/selfexe_guest.c 2>/dev/null || { echo "selfexe: cannot build a static guest, skipping"; exit 0; }
mkdir -p "$T/rf/bin"; cp "$T/g" "$T/rf/bin/g"
rc=0
check() {   # check NAME OUTPUT EXPECTED-LINE
    echo "$2" | grep -qxF "$3" || { echo "selfexe: $1: expected \"$3\" in:"; echo "$2"; rc=1; }
}
out=$(cd "$T" && timeout 60 "$B/pemu" ./g 2>&1)
check "no rootfs" "$out" "exe: $T/g"
check "no rootfs" "$out" "exe by pid: $T/g"
check "no rootfs" "$out" "stat same file: yes"
check "no rootfs" "$out" "open exe: elf"
check "no rootfs" "$out" "cmdline: ok"
check "no rootfs" "$out" "child ok x"
check "no rootfs" "$out" "exec child status: 0"
check "no rootfs" "$out" "PR_SET_SECCOMP: Permission denied"
check "no rootfs" "$out" "PR_GET_SECCOMP: 0"
check "no rootfs" "$out" "seccomp(): Permission denied"
out=$(timeout 60 "$B/pemu" -r "$T/rf" /bin/g 2>&1)
check "rootfs" "$out" "exe: /bin/g"
check "rootfs" "$out" "exe by pid: /bin/g"
check "rootfs" "$out" "stat same file: yes"
check "rootfs" "$out" "open exe: elf"
check "rootfs" "$out" "cmdline: ok"
check "rootfs" "$out" "child ok x"
check "rootfs" "$out" "exec child status: 0"
check "rootfs" "$out" "PR_SET_SECCOMP: Permission denied"
[ $rc = 0 ] && echo "selfexe pemu: ok" || echo "selfexe pemu: FAILED"
exit $rc

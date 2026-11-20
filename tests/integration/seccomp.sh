#!/bin/sh
# The guest's own seccomp (src/proot/seccomp.inc.h) seen by a real x86_64 guest under pemu: no_new_privs, ERRNO / TRAP / ALLOW
# filters, a SIGSYS handler that sets the result, inheritance by fork and by execve (the filters are handed to the new image), KILL_PROCESS,
# strict mode.  Needs gcc and an x86_64 host (the guest runs natively-built code; no rootfs, no clang).  usage: tests/integration/seccomp.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "seccomp: x86_64 host required, skipping"; exit 0; }
command -v gcc >/dev/null || { echo "seccomp: gcc missing, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -no-pie -mno-sse -o "$T/guest" tests/guests/seccomp_guest.c
ulimit -c 0
HOME=$T timeout 60 "$B/pemu" "$T/guest" > "$T/out" 2>&1 || true
if diff -u tests/data/seccomp.expected "$T/out"; then echo "seccomp (x86_64 guest): ok"; else echo "seccomp (x86_64 guest): FAILED"; exit 1; fi

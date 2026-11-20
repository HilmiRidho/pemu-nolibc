#!/bin/sh
# brk must give back zero pages after a shrink (x86_64 guest under pemu and the JIT run by the embedded interpreter; x86_64 host, plain gcc + static libc):
#   pemu kept the whole heap mapped, so a shrink of the break (glibc's malloc trim) left the old data above it and a later growth showed it again.
#   glibc calloc() does not clear memory it just got from sbrk(), so calloc returned non-zero memory; GCC's hash tables then held garbage
#   and cc1 died with "internal compiler error: Segmentation fault" in htab_delete / __libc_free.
# Skips (exit 0) when the host is not x86_64 or gcc cannot link statically.  usage: tests/integration/brk.sh [BUILD_DIR]
cd "$(dirname "$0")/../.."
B=${1:-build}
B=$(cd "$B" && pwd)
[ "$(uname -m)" = x86_64 ] || { echo "brk: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/guests/brk_guest.c 2>/dev/null || { echo "brk: cannot build a static guest, skipping"; exit 0; }
rc=0
"$T/g" > "$T/native" 2>&1 || { echo "brk: the guest fails natively:"; cat "$T/native"; exit 1; }
out=$(timeout 120 "$B/pemu" "$T/g" 2>&1) || { echo "brk: calloc returned stale memory under pemu:"; echo "$out"; rc=1; }
[ "$out" = "$(cat "$T/native")" ] || { echo "brk: output differs from native:"; echo "$out"; rc=1; }
[ $rc = 0 ] && echo "brk: shrink then growth gives zero pages: ok"
exit $rc

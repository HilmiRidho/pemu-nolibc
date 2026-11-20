#!/bin/sh
# PEMU_HOST_STATS (host/pemu_host.h): how often pemu called the host kernel, printed when the guest exits (x86_64 host, plain gcc).
#   The guest (tests/guests/hoststats_guest.c) has no libc and makes ONE write(1) of its own, so its line must read "1 x write (1)";
#   the rest are the emulator's own set-up calls (memory map, ELF loader: openat + pread64, auxv random).
#   Also checked: the variable changes nothing about what the guest prints, prints nothing when unset, and =/path writes <path>.<pid>.
# Skips (exit 0) when the host is not x86_64 or gcc cannot build the guest.  usage: tests/integration/host_stats.sh [BUILD_DIR]
cd "$(dirname "$0")/../.."
B=${1:-build}
B=$(cd "$B" && pwd)
[ "$(uname -m)" = x86_64 ] || { echo "host_stats: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -no-pie -nostdlib -static -o "$T/g" tests/guests/hoststats_guest.c 2>/dev/null \
    || { echo "host_stats: cannot build the guest, skipping"; exit 0; }
rc=0
fail() { echo "host_stats: $1"; rc=1; }

plain=$("$B/pemu" "$T/g" 2>"$T/err0"); [ -s "$T/err0" ] && fail "stderr is not empty without PEMU_HOST_STATS: $(cat "$T/err0")"
echo "$plain" | grep -q "halo tanpa libc" || fail "the guest did not run: $plain"

# 1. =1 -> stderr; the guest's stdout is unchanged
with=$(PEMU_HOST_STATS=1 "$B/pemu" "$T/g" 2>"$T/err1")
[ "$with" = "$plain" ] || fail "PEMU_HOST_STATS changed the guest's output"
grep -Eq '^pemu host: [0-9]+ calls into the host kernel, [0-9]+ kinds$' "$T/err1" || fail "no summary line: $(cat "$T/err1")"
grep -Eq '^pemu host: +1 x  write \(1\)$' "$T/err1" || fail "expected exactly one write(1): $(cat "$T/err1")"
grep -Eq '^pemu host: +[0-9]+ x  mmap \(9\)$' "$T/err1" || fail "no mmap line: $(cat "$T/err1")"
grep -Eq '^pemu host: +[0-9]+ x  openat \(257\)$' "$T/err1" || fail "no openat line (the emulator opens the guest's ELF): $(cat "$T/err1")"
grep -Eq '^pemu host: +[0-9]+ x  pread64 \(17\)$' "$T/err1" || fail "no pread64 line (the ELF loader reads it): $(cat "$T/err1")"

# 2. =/abs/path -> one file per process, <path>.<pid>, nothing on stderr
PEMU_HOST_STATS="$T/st" "$B/pemu" "$T/g" >/dev/null 2>"$T/err2"
[ -s "$T/err2" ] && fail "stderr is not empty with a file path: $(cat "$T/err2")"
set -- "$T"/st.*
[ -f "$1" ] || fail "no $T/st.<pid> file"
[ -f "$1" ] && { grep -Eq '^pemu host: +1 x  write \(1\)$' "$1" || fail "file lacks the write line: $(cat "$1")"; }

[ $rc = 0 ] && echo "host_stats: ok" || echo "host_stats: FAILED"
exit $rc

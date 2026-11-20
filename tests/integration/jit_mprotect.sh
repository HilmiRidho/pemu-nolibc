#!/bin/sh
# JIT cache invalidation by mmap / mprotect / munmap (tests/guests/jit_mprotect_guest.c): replaced and re-protected code must run its NEW bytes, and the calls that cannot
# change translated code (r-x over r-x, big mappings far from any code) must not flush the cache (Wine does thousands of them; every flush throws all translations away).
# x86_64 host with gcc and a static libc; the generated AArch64 code runs in the embedded interpreter (-DPEMU_JIT_EMBED, see jit_embed.sh).  usage: tests/integration/jit_mprotect.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = x86_64 ] || { echo "jit_mprotect: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
echo 'int main(void){return 0;}' > "$T/s.c"
$CC -static -o "$T/s" "$T/s.c" 2>/dev/null || { echo "jit_mprotect: no static libc, skipping"; exit 0; }
SRC=${PEMU_SRC:-.}
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_EMBED -c "$SRC/tests/oracle/aarch64/oracle.c" -o "$T/a64.o"
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_JIT_EMBED -c "$SRC/src/pemu.c" -o "$T/x64.o"
$CC -o "$T/pemu_embed" "$T/x64.o" "$T/a64.o" -lm -lpthread
$CC -O1 -w -static -o "$T/g" tests/guests/jit_mprotect_guest.c
"$T/g" > "$T/native"
timeout 120 "$T/pemu_embed" -nojit "$T/g" > "$T/nojit"
PEMU_JIT_STATS=1 timeout 300 "$T/pemu_embed" "$T/g" > "$T/jit" 2> "$T/stats"
rc=0
for v in nojit jit; do
    if diff -u "$T/native" "$T/$v" > "$T/diff"; then echo "jit_mprotect ($v = native): ok"; else cat "$T/diff"; echo "jit_mprotect ($v = native): FAILED"; rc=1; fi
done
fl=$(sed -n 's/.* \([0-9][0-9]*\) cache flushes.*/\1/p' "$T/stats" | head -1)
# expected: r-x -> rw- (1), MAP_FIXED over translated code (2), munmap of translated code (3); the rwx page is never translated.  A few more would be harmless, 20+ is the bug.
if [ -n "$fl" ] && [ "$fl" -le 6 ]; then echo "jit_mprotect: $fl cache flushes (r-x over r-x and far mappings do not flush): ok"; else echo "jit_mprotect: $fl cache flushes: FAILED"; rc=1; fi
exit $rc

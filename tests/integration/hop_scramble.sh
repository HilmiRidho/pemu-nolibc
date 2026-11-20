#!/bin/sh
# The Linux ABI layer (src/) asks the host for operations by name (HOP_openat ...), never by a Linux syscall number; host/pemu_hop.h is the one place
# that says what number a name has on this host.  This test proves it: it builds pemu with PEMU_HOP_SCRAMBLE=1, where every HOP_ value is NOT the Linux
# number (host_syscall() translates it back, anything that is not a HOP_ value becomes ENOSYS), and runs guests that exercise the syscall layer on it.
# A raw Linux number left in src/ (hs(425, ...), a table of guest numbers handed to the host ...) makes one of them fail.
# The static side of the same rule is tools/check_host_seam.py (no SYS_x, no literal number) and tools/gen_hop.py --check (the table is current).
# Skips (exit 0) when python3 or the compiler is missing.   usage: tests/integration/hop_scramble.sh [BUILD_DIR]   (BUILD_DIR is not used: it builds its own)
cd "$(dirname "$0")/.." && cd .. || exit 2
CC=${CC:-gcc}
command -v python3 >/dev/null 2>&1 || { echo "hop_scramble: python3 missing, skipping"; exit 0; }
command -v "$CC" >/dev/null 2>&1 || { echo "hop_scramble: no compiler ($CC), skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
OUT="$T/b" CC="$CC" PEMU_HOP_SCRAMBLE=1 SKIP_TESTS=1 sh ./build.sh > "$T/build.log" 2>&1 || { cat "$T/build.log"; echo "hop_scramble: the scrambled build FAILED"; exit 1; }
# the scrambled build must really differ from the plain one (otherwise the define did nothing and the test proves nothing)
OUT="$T/p" CC="$CC" SKIP_TESTS=1 sh ./build.sh > "$T/plain.log" 2>&1 || { cat "$T/plain.log"; echo "hop_scramble: the plain build FAILED"; exit 1; }
cmp -s "$T/b/pemu" "$T/p/pemu" && { echo "hop_scramble: the scrambled binary equals the plain one, PEMU_HOP_SCRAMBLE had no effect"; exit 1; }
rc=0
for t in seccomp uring sock ns owner cred suid brk pidfence selfexe host_stats elf_bad; do
  out=$(sh "tests/integration/$t.sh" "$T/b" 2>&1) || { echo "$out"; echo "hop_scramble: $t FAILED on the scrambled build"; rc=1; }
done
[ $rc = 0 ] && echo "hop_scramble: ok (12 guest tests pass with HOP_ numbers that are not Linux numbers)"
exit $rc

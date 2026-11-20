#!/bin/sh
# A guest SIGSEGV handler without sigaltstack, and a stack overflow: Linux cannot build the signal frame and kills the process with SIGSEGV.  pemu
# used to deliver the fault to the handler again and again (a hang at 100% CPU).  Expect: killed by SIGSEGV (status 139) quickly, with a line in the log.
# x86_64 guest under `pemu`.  Needs clang + lld, x86_64 host.
# usage: tests/integration/sigframe.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
skip() { echo "sigframe: $1, skipping"; [ -z "$PEMU_REQUIRE_TESTS" ] || { echo "sigframe: FAILED (PEMU_REQUIRE_TESTS is set, a skip is not allowed)"; exit 1; }; exit 0; }
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "sigframe: x86_64 host required, skipping"; exit 0; }
command -v clang >/dev/null && command -v ld.lld >/dev/null || skip "clang/lld missing"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
clang --target=x86_64-linux-gnu -O0 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -fuse-ld=lld -o "$T/g" tests/guests/sigframe_guest.c
rc=0
for jit in 1 0; do
    H=$T/home$jit; mkdir -p "$H"
    set +e; HOME=$H PEMU_JIT=$jit timeout 20 "$B/pemu" "$T/g" >/dev/null 2>&1; st=$?; set -e
    [ "$st" = 139 ] || { echo "sigframe: FAILED (PEMU_JIT=$jit): status $st, expected 139 (killed by SIGSEGV; 124 = hang, 7 = the handler ran)"; rc=1; }
    grep -q 'cannot build the signal frame' "$H/.pemu/pemu.log" 2>/dev/null || { echo "sigframe: FAILED (PEMU_JIT=$jit): no log line"; rc=1; }
done
[ $rc = 0 ] && echo "sigframe pemu: ok"
exit $rc

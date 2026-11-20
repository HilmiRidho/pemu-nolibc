#!/bin/sh
# End-to-end test: runs one freestanding guest (tests/guests/e2e_guest.c) under pemu on an x86_64 host. Needs clang + lld.  usage: tests/integration/e2e.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
# PEMU_REQUIRE_TESTS=1 (set by CI): a missing tool is a failure instead of a skip, so a test that did not run cannot be mistaken for one that passed.
skip() { echo "e2e: $1, skipping"; [ -z "$PEMU_REQUIRE_TESTS" ] || { echo "e2e: FAILED (PEMU_REQUIRE_TESTS is set, a skip is not allowed)"; exit 1; }; exit 0; }
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "e2e: x86_64 host required, skipping"; exit 0; }
command -v clang >/dev/null && command -v ld.lld >/dev/null || skip "clang/lld missing"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
run() {   # target emulator
    rf=$T/rf-$2; mkdir -p "$rf/etc" "$rf/tmp" "$rf/bin"
    clang --target=$1 -O1 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -fuse-ld=lld -o "$rf/bin/guest" tests/guests/e2e_guest.c
    printf 'welcome\n' > "$rf/etc/motd"; ln -s motd "$rf/etc/link"
    PEMU_FAKE_PROC=1 timeout 60 "$B/$2" login "$rf" -w / /bin/guest > "$T/out-$2" 2>&1 || true
    # the fake /proc/stat is read as 5 bytes "cpu  "
    if diff -u tests/data/e2e.expected "$T/out-$2"; then echo "e2e $2 ($1): ok"; else echo "e2e $2 ($1): FAILED"; return 1; fi
}
rc=0
run x86_64-linux-gnu pemu || rc=1
exit $rc

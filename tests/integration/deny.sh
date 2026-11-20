#!/bin/sh
# The shared refusal table (src/proot/deny.inc.h) as seen by an x86_64 guest under `pemu`.
# ptrace, setns, unshare(CLONE_NEW*), open_by_handle_at and the new mount API (open_tree, move_mount, fsopen, fsconfig, fsmount, fspick,
# mount_setattr, open_tree_attr) are EPERM; name_to_handle_at resolves its path in the rootfs.  Every call in tests/guests/deny_guest.c is harmless
# on its own (paths that cannot exist, invalid fds), so a broken policy fails the test instead of touching this machine.
# Run twice per guest: without a rootfs (name_to_handle_at passes through to the host) and inside one.  Needs clang + lld, x86_64 host.
# usage: tests/integration/deny.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
# PEMU_REQUIRE_TESTS=1 (set by CI): a missing tool is a failure instead of a skip, so a test that did not run cannot be mistaken for one that passed.
skip() { echo "deny: $1, skipping"; [ -z "$PEMU_REQUIRE_TESTS" ] || { echo "deny: FAILED (PEMU_REQUIRE_TESTS is set, a skip is not allowed)"; exit 1; }; exit 0; }
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "deny: x86_64 host required, skipping"; exit 0; }
command -v clang >/dev/null && command -v ld.lld >/dev/null || skip "clang/lld missing"
[ ! -e /pemu-deny-test-only-in-rootfs ] && [ ! -e /pemu-deny-test-does-not-exist ] || { echo "deny: the probe paths exist on this host, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
rc=0
# One probe is passed to the host on purpose: unshare(1) (no NEW flag) must reach it.  A plain host answers EINVAL; a host that filters
# unshare (container seccomp profile, some sandboxes) answers EPERM before the kernel looks at the flags.  Ask the host first and expect
# what it really answers, otherwise the test would depend on the machine's policy rather than on pemu.
cat > "$T/hostprobe.c" <<'EOC'
#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
int main(void) { return unshare(1) == -1 && errno == EINVAL ? 0 : 1; }
EOC
if clang -O0 -o "$T/hostprobe" "$T/hostprobe.c" 2>/dev/null && "$T/hostprobe"; then host_unshare=-22
else host_unshare=-1; echo "deny: this host answers EPERM to unshare(1) (seccomp?); the pass-through probe expects that"; fi
run() {   # target emulator
    g=$T/guest-$2; rf=$T/rf-$2; mkdir -p "$rf/bin"
    clang --target=$1 -O1 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -fuse-ld=lld -o "$g" tests/guests/deny_guest.c
    cp "$g" "$rf/bin/guest"; : > "$rf/pemu-deny-test-only-in-rootfs"
    timeout 60 "$B/$2" "$g" > "$T/nr-$2" 2>&1 || true
    timeout 60 "$B/$2" login "$rf" -w / /bin/guest > "$T/rf-out-$2" 2>&1 || true
    for v in nr:norootfs rf-out:rootfs; do
        f=${v%%:*}; e=tests/data/deny.expected.${v##*:}
        sed "s/^\(unshare no NEW flag (passes to the host)\)=-22\$/\1=$host_unshare/" "$e" > "$T/expected"
        if diff -u "$T/expected" "$T/$f-$2" >&2; then echo "deny $2 ($1, ${v##*:}): ok"; else echo "deny $2 ($1, ${v##*:}): FAILED"; rc=1; fi
    done
}
run x86_64-linux-gnu pemu
exit $rc

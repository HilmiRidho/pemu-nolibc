#!/bin/sh
# A set-uid file must start with effective+saved uid = its owner (fake root: 0), real uid unchanged.
# dbus-daemon-launch-helper refuses to run otherwise ("The permission of the setuid helper is not correct").
# Regression: pemu_exec_ids() ran AFTER the --ids option of the new image was written, so the set-uid effect was lost.
# usage: tests/integration/suid.sh [BUILD_DIR]
cd "$(dirname "$0")/../.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "suid: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/guests/suid_guest.c 2>/dev/null || { echo "suid: cannot build a static guest, skipping"; exit 0; }
mkdir -p "$T/rf/usr/lib" "$T/home"; cp "$T/g" "$T/rf/g.bin"; cp "$T/g" "$T/rf/usr/lib/h"; chmod -R a+rX "$T"
P="$B/pemu"; export HOME="$T/home"; rc=0
chmod 4754 "$T/rf/usr/lib/h"
out=$(timeout 60 "$P" -r "$T/rf" --root-id --no-default-binds /g.bin 2>&1)
[ "$out" = "uid r=101 e=0 s=0" ] || { echo "suid: set-uid file: want 'uid r=101 e=0 s=0', got: $out"; rc=1; }
chmod 0755 "$T/rf/usr/lib/h"
out=$(timeout 60 "$P" -r "$T/rf" --root-id --no-default-binds /g.bin 2>&1)
[ "$out" = "uid r=101 e=101 s=101" ] || { echo "suid: plain file: want 'uid r=101 e=101 s=101', got: $out"; rc=1; }
[ $rc = 0 ] && echo "suid: ok"
exit $rc

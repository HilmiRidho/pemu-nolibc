#!/bin/sh
# SCM_CREDENTIALS / SO_PEERCRED under a fake root (x86_64 guest under pemu, x86_64 host, plain gcc, run as an unprivileged user):
# GDBus sends its (fake) uid/gid in SCM_CREDENTIALS when it connects; the kernel answers EPERM for ids that are not the sender's real ones.
# The ids must be sent as host ids and shown back as the fake ones.  Skips as real root (the kernel accepts anything) or without setpriv.
# usage: tests/integration/cred.sh [BUILD_DIR]
cd "$(dirname "$0")/../.."
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "cred: x86_64 host required, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/guests/cred_guest.c 2>/dev/null || { echo "cred: cannot build a static guest, skipping"; exit 0; }
mkdir -p "$T/rf"; cp "$T/g" "$T/rf/g.bin"; chmod -R a+rX "$T"
if [ "$(id -u)" = 0 ]; then
    command -v setpriv >/dev/null 2>&1 || { echo "cred: setpriv missing, skipping"; exit 0; }
    RUN="setpriv --reuid=1000 --regid=1000 --clear-groups"
    cp "$B/pemu" "$T/pemu"; chmod 755 "$T/pemu"; P="$T/pemu"
else RUN=""; P="$B/pemu"; fi
exp='send creds uid=0 gid=0: ok
recv creds uid=0 gid=0
peercred uid=0 gid=0'
rc=0
out=$($RUN timeout 60 "$P" --root-id "$T/g" 2>&1)
[ "$out" = "$exp" ] || { echo "cred: fake root, no rootfs:"; echo "$out"; rc=1; }
out=$($RUN timeout 60 "$P" -r "$T/rf" --root-id --no-default-binds /g.bin 2>&1)
[ "$out" = "$exp" ] || { echo "cred: fake root, rootfs:"; echo "$out"; rc=1; }
exp2='send creds uid=0 gid=0: ok
dropped to uid=101
recv creds uid=0 gid=0
peercred uid=0 gid=0'
out=$($RUN timeout 60 "$P" -r "$T/rf" --root-id --no-default-binds /g.bin 101 2>&1)
[ "$out" = "$exp2" ] || { echo "cred: fake root drops to uid 101 (dbus-daemon), rootfs:"; echo "$out"; rc=1; }
[ $rc = 0 ] && echo "cred: ok"
exit $rc

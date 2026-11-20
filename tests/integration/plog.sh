#!/bin/sh
# The negative-event log (src/proot/pemulog.inc.h): ~/.pemu/pemu.log is written by every run, with no switch, and never appears in the
# guest's own output.  Unsupported syscall, ENOSYS answers, refusals, an unsupported instruction and a load error each leave one line; a repeated
# event is written once and summed up as "repeated xN" when the process exits.  HOME is a temporary directory, so the real log is not touched.
# x86_64 guest under `pemu`.  Needs clang + lld, x86_64 host.
# usage: tests/integration/plog.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
# PEMU_REQUIRE_TESTS=1 (set by CI): a missing tool is a failure instead of a skip, so a test that did not run cannot be mistaken for one that passed.
skip() { echo "plog: $1, skipping"; [ -z "$PEMU_REQUIRE_TESTS" ] || { echo "plog: FAILED (PEMU_REQUIRE_TESTS is set, a skip is not allowed)"; exit 1; }; exit 0; }
B=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "plog: x86_64 host required, skipping"; exit 0; }
command -v clang >/dev/null && command -v ld.lld >/dev/null || skip "clang/lld missing"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
rc=0
chk() { if grep -q -- "$2" "$1"; then :; else echo "plog $3: FAILED, no line matching: $2"; sed 's/^/    /' "$1" >&2; rc=1; fi; }
run() {   # target emulator
    g=$T/guest-$2; H=$T/home-$2; mkdir -p "$H"; L=$H/.pemu/pemu.log
    clang --target=$1 -O1 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -fuse-ld=lld -o "$g" tests/guests/plog_guest.c
    HOME=$H timeout 60 "$B/$2" "$g" > "$T/out-$2" 2>&1 || { echo "plog $2: the guest failed"; rc=1; }
    [ ! -s "$T/out-$2" ] || { echo "plog $2: FAILED, the guest saw output from pemu:"; cat "$T/out-$2"; rc=1; }
    [ -f "$L" ] || { echo "plog $2: FAILED, no $L"; rc=1; return; }
    [ "$(stat -c %a "$L")" = 600 ] || { echo "plog $2: FAILED, the log should be 0600"; rc=1; }
    chk "$L" ' unsupported-syscall: syscall 1000 (?) has no implementation' "$2 unsupported syscall"
    chk "$L" ' unsupported-syscall: repeated x6: syscall 1000' "$2 repeated summary"
    [ "$(grep -c ' unsupported-syscall: syscall 1000' "$L")" = 1 ] || { echo "plog $2: FAILED, the repeated syscall was written more than once"; rc=1; }
    chk "$L" ' enosys: syscall [0-9]* (rseq) answered ENOSYS' "$2 rseq"
    chk "$L" ' refused: syscall [0-9]* (ptrace): refused on purpose' "$2 ptrace"
    chk "$L" ' refused: syscall [0-9]* (unshare): new namespace refused' "$2 unshare"
    chk "$L" "pid=[0-9]* \[guest-$2\]" "$2 program name"
    # an instruction pemu does not implement
    HOME=$H timeout 60 "$B/$2" "$g" ill > /dev/null 2>&1 || true
    chk "$L" ' unsupported-insn: .*SIGILL' "$2 unsupported instruction"
    # a file that is not an ELF of this guest
    echo junk > "$T/junk"; HOME=$H "$B/$2" "$T/junk" > /dev/null 2>&1 || true
    chk "$L" ' error: .*junk: not an' "$2 load error"
    # inside a rootfs the log still goes to HOME/.pemu (next to rootfs/), not into the rootfs
    rf=$T/rf-$2; mkdir -p "$rf/bin"; cp "$g" "$rf/bin/guest"
    HOME=$H timeout 60 "$B/$2" login "$rf" -w / /bin/guest > "$T/rfout-$2" 2>&1 || true
    [ ! -s "$T/rfout-$2" ] || { echo "plog $2: FAILED, output inside the rootfs:"; cat "$T/rfout-$2"; rc=1; }
    [ -z "$(find "$rf" -name 'pemu.log*')" ] || { echo "plog $2: FAILED, a log file inside the rootfs"; rc=1; }
    [ "$(grep -c ' unsupported-syscall: syscall 1000' "$L")" = 2 ] || { echo "plog $2: FAILED, the rootfs run did not add its line"; rc=1; }
    # a guest that changes HOME (execve with another environment) cannot move the log: the re-exec'd pemu is told the first one's path
    HOME=$H timeout 60 "$B/$2" "$g" exec > "$T/xout-$2" 2>&1 || { echo "plog $2: the execve run failed"; rc=1; }
    [ ! -e /nonexistent-plog-home ] || { echo "plog $2: FAILED, the log followed the guest's HOME"; rc=1; }
    [ "$(grep -c ' unsupported-syscall: syscall 1000' "$L")" = 3 ] || { echo "plog $2: FAILED, the re-exec'd process did not log to the first path"; rc=1; }
    echo "plog $2: $( [ $rc = 0 ] && echo ok || echo FAILED )"
}
run x86_64-linux-gnu pemu
exit $rc

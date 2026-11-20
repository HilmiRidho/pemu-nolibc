#!/bin/sh
# JOPT_ACQ (src/jit/x86_64-aarch64/guest_state.inc.h): on a host without FEAT_LRCPC2 a multi-threaded x86_64 guest uses ldar / ldapr + stlr for aligned integer accesses
# and the dmb sequence for misaligned ones.  tests/guests/acq_guest.c makes loads and stores of every size, sign extension and alignment after it has created a thread.
# The generated AArch64 code runs on the interpreter of tests/oracle/aarch64/ (-DPEMU_JIT_EMBED), which aborts on a misaligned ldar / ldapr / stlr like the hardware does.
# The checksum must be the same natively, under the interpreter and under the JIT in each memory-ordering mode.  x86_64 host with gcc and a static libc.
# usage: tests/integration/acq.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = x86_64 ] || { echo "acq: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
$CC -O1 -w -static -o "$T/g" tests/guests/acq_guest.c -lpthread 2>/dev/null || { echo "acq: cannot build a static guest, skipping"; exit 0; }
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_EMBED -c tests/oracle/aarch64/oracle.c -o "$T/a64.o"
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_JIT_EMBED -c src/pemu.c -o "$T/x64.o"
$CC -o "$T/pemu_embed" "$T/x64.o" "$T/a64.o" -lm -lpthread
want=$("$T/g" | tail -1)
# JOPT_RCPC (1024) off so that the ACQ path is used; ACQ (65536) off for the dmb path
ALL=131071; ACQ=65536; RCPC=1024
rc=0
run() {  # name, env..., (stats must show N accesses with ldar/ldapr/stlr: 0 or >0)
    name=$1; shift; need=$1; shift
    got=$(env "$@" PEMU_JIT_STATS=1 timeout 300 "$T/pemu_embed" "$T/g" 2> "$T/err" | tail -1)
    n=$(sed -n 's/^pemu jit: \([0-9]*\) guest accesses with ldar.*/\1/p' "$T/err")
    ok=0
    if [ "$got" = "$want" ]; then
        if [ "$need" = zero ]; then [ "$n" = 0 ] && ok=1; else [ "${n:-0}" -gt 0 ] && ok=1; fi
    fi
    if [ $ok = 1 ]; then echo "acq $name: ok"; else echo "acq $name: FAILED ($got, ${n:-?} accesses)"; head -3 "$T/err"; rc=1; fi
}
[ "$("$T/pemu_embed" -nojit "$T/g" | tail -1)" = "$want" ] && echo "acq interpreter: ok" || { echo "acq interpreter: FAILED"; rc=1; }
run "dmb path"       zero PEMU_JIT_OPT=$((ALL - ACQ - RCPC))
run "ldar/stlr"      some PEMU_JIT_OPT=$((ALL - RCPC))
run "ldapr/stlr"     some PEMU_JIT_OPT=$((ALL - RCPC)) PEMU_JIT_LDAPR=1
run "ldapur/stlur"   zero PEMU_JIT_OPT=$ALL
exit $rc

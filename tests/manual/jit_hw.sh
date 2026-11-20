#!/bin/sh
# The AArch64 JIT on real AArch64 hardware. Everything else in this repository runs the code the JIT emits on an interpreter (tests/oracle/aarch64/)
# on x86_64; this is the one test that executes it on a CPU. Two steps, because an aarch64 device normally has no x86_64 static libc to build the guests with:
#
#   1. on an x86_64 machine (gcc, static libc, python3):   tests/manual/jit_hw.sh make DIR
#        builds the same guests as tests/integration/jit_embed.sh and records what they print when run natively (the reference, from a real x86_64 CPU)
#   2. copy DIR to the aarch64 device (Termux, Debian arm64, ...) together with a pemu built there:   tests/manual/jit_hw.sh run DIR [PEMU]
#        runs every guest under `pemu -nojit` (interpreter) and under `pemu` (JIT) and compares both with the reference
#
# Pass: every guest prints exactly the reference lines (lines starting with '~' are flags the SDM calls undefined: ignored) under both, and the JIT was
# really on (PEMU_JIT_STATS produced a report). Exit 0 only then.  HW_FORCE=1 runs `run` on a non-aarch64 host (only to try the script itself: no JIT there).
set -e
mode=$1 D=$2
[ -n "$D" ] || { echo "usage: $0 make DIR | run DIR [PEMU]" >&2; exit 2; }
cd "$(dirname "$0")/../.."
GUESTS="sse4_reg sse4_mem alu muldiv ext cvt fuse vec pmul pfp pin inplace hc hci ro shuf seg"
case "$mode" in
make)
    [ "$(uname -m)" = x86_64 ] || { echo "jit_hw make: needs an x86_64 machine" >&2; exit 2; }
    CC=${CC:-gcc}; mkdir -p "$D"
    echo 'int main(void){return 0;}' > "$D/s.c"; $CC -static -o "$D/s" "$D/s.c" 2>/dev/null || { echo "jit_hw make: no static libc" >&2; exit 2; }; rm -f "$D/s" "$D/s.c"
    command -v python3 >/dev/null || { echo "jit_hw make: python3 needed (pin, inplace)" >&2; exit 2; }
    for g in $GUESTS; do
        f=tests/guests/jit_$g.c; fl=
        case $g in sse4_*|vec|pmul|pfp|shuf|hci) fl=-msse4.2 ;; ro|seg) fl=-mno-red-zone ;; esac
        [ $g = pin ] && { python3 tests/guests/jit_pin_gen.py > "$D/jit_pin.c"; f=$D/jit_pin.c; fl=-no-pie; }
        [ $g = inplace ] && { python3 tests/guests/jit_inplace_gen.py > "$D/jit_inplace.c"; f=$D/jit_inplace.c; }
        $CC -O1 -w $fl -static -o "$D/g_$g" "$f" -lm
        "$D/g_$g" | grep -v '^~' > "$D/native-$g"
        echo "jit_hw make: $g ($(wc -l < "$D/native-$g") lines)"
    done
    rm -f "$D/jit_pin.c" "$D/jit_inplace.c"
    echo "jit_hw make: done; copy $D to the aarch64 device and run: tests/manual/jit_hw.sh run $D"
    ;;
run)
    P=${3:-build/pemu}
    [ "$(uname -m)" = aarch64 ] || [ -n "$HW_FORCE" ] || { echo "jit_hw run: not an aarch64 host (HW_FORCE=1 only tries the script)" >&2; exit 2; }
    [ -x "$P" ] || { echo "jit_hw run: $P not found (build pemu on this device first)" >&2; exit 2; }
    W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; rc=0
    echo "host: $(uname -m), kernel $(uname -r), $("$P" --version 2>&1 | head -1)"
    for g in $GUESTS; do
        [ -x "$D/g_$g" ] || { echo "jit_hw $g: FAILED, $D/g_$g missing"; rc=1; continue; }
        timeout 300 "$P" -nojit "$D/g_$g" 2>/dev/null | grep -v '^~' > "$W/nojit-$g" || true
        rm -f "$W/stats-$g".*
        PEMU_JIT_STATS="$W/stats-$g" timeout 300 "$P" "$D/g_$g" 2>/dev/null | grep -v '^~' > "$W/jit-$g" || true
        for v in nojit jit; do
            if diff -u "$D/native-$g" "$W/$v-$g" > "$W/diff"; then echo "jit_hw $g ($v = native): ok"
            else head -20 "$W/diff"; echo "jit_hw $g ($v = native): FAILED"; rc=1; fi
        done
        if ls "$W/stats-$g".* > /dev/null 2>&1; then :
        elif [ -n "$HW_FORCE" ]; then :
        else echo "jit_hw $g: FAILED, no PEMU_JIT_STATS report: the JIT did not run (PEMU_JIT=0 set? JIT init failed?)"; rc=1; fi
    done
    [ $rc = 0 ] && echo "jit_hw: all guests identical to native under the interpreter and the JIT" || echo "jit_hw: FAILED" >&2
    exit $rc
    ;;
*) echo "usage: $0 make DIR | run DIR [PEMU]" >&2; exit 2 ;;
esac

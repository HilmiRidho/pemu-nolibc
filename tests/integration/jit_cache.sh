#!/bin/sh
# Persistent translation cache (PEMU_JIT_CACHE, src/jit/x86_64-aarch64/jcache.inc.h) on the JIT test guests of jit_embed.sh, in the same setting: the AArch64 code the JIT
# generates is executed by the interpreter of tests/oracle/aarch64/ (-DPEMU_JIT_EMBED); the translator and the cache themselves run natively on the x86_64 host, so what is
# checked here is that a stored block, put into another run's code cache, is exactly what the translator would have emitted there.
#   1. a run with an empty cache directory records blocks and prints what a run without the cache prints;
#   2. PEMU_JIT_CACHE_VERIFY=1 translates every stored block again and compares it word for word with the relocated copy (a helper address, a branch to the exit
#      stub or a guest-address load that was not relocated shows up as a MISMATCH): none may differ, and some must have been compared;
#   3. a normal run then loads blocks (the cache is used) and prints the same again;
#   4. blocks of one program are not used for another program at the same addresses (the guest bytes differ);
#   5. a damaged / torn / foreign file, and an unsafe directory, change nothing but speed;
#   6. several processes writing the same cache at once.
# x86_64 host with gcc and a static libc.  usage: tests/integration/jit_cache.sh
set -e
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = x86_64 ] || { echo "jit_cache: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
echo 'int main(void){return 0;}' > "$T/s.c"
$CC -static -o "$T/s" "$T/s.c" 2>/dev/null || { echo "jit_cache: no static libc, skipping"; exit 0; }
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_EMBED -c tests/oracle/aarch64/oracle.c -o "$T/a64.o"
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_JIT_EMBED -c src/pemu.c -o "$T/x64.o"
$CC -o "$T/pemu_embed" "$T/x64.o" "$T/a64.o" -lm -lpthread
E="$T/pemu_embed"
rc=0
fail() { echo "jit_cache $1: FAILED"; rc=1; }
num() { sed -n "s/.*pemu jit cache:.*[^0-9]\([0-9][0-9]*\) $1.*/\1/p" "$2" | head -1; }     # a figure of the statistics line
newdir() { rm -rf "$1"; mkdir -p "$1"; chmod 700 "$1"; }
run() { # run DIR GUEST OUT ERR [extra env assignments via ENVX]
    env $ENVX PEMU_JIT_CACHE="$1" PEMU_JIT_STATS=1 timeout 300 "$E" "$2" > "$3" 2> "$4" || true
}

GUESTS="sse4_reg sse4_mem alu muldiv ext cvt fuse vec pmul pfp hc hci ro shuf seg"
for g in $GUESTS; do
    case $g in sse4_*|vec|pmul|pfp|shuf|hci) fl=-msse4.2 ;; ro|seg) fl=-mno-red-zone ;; *) fl= ;; esac
    $CC -O1 -w $fl -static -o "$T/g_$g" tests/guests/jit_$g.c -lm
done
if command -v python3 >/dev/null; then
    python3 tests/guests/jit_pin_gen.py > "$T/jit_pin.c" && $CC -O1 -w -no-pie -static -o "$T/g_pin" "$T/jit_pin.c" -lm && GUESTS="$GUESTS pin"
    python3 tests/guests/jit_inplace_gen.py > "$T/jit_inplace.c" && $CC -O1 -w -static -o "$T/g_inplace" "$T/jit_inplace.c" -lm && GUESTS="$GUESTS inplace"
fi

ENVX=
for g in $GUESTS; do
    D="$T/c-$g"; newdir "$D"
    timeout 300 "$E" "$T/g_$g" > "$T/ref-$g" 2>/dev/null || true                          # reference: no cache
    ENVX="PEMU_JIT_CACHE_VERIFY=1" run "$D" "$T/g_$g" "$T/o1" "$T/e1"        # empty cache: nothing to compare, but the blocks get random offsets (see jc_jitter)
    if cmp -s "$T/ref-$g" "$T/o1" && [ "$(num recorded "$T/e1")" -gt 0 ] 2>/dev/null; then :; else fail "$g (record)"; continue; fi
    ENVX="PEMU_JIT_CACHE_VERIFY=1" run "$D" "$T/g_$g" "$T/o2" "$T/e2"
    ok=$(num equal "$T/e2"); bad=$(num MISMATCH "$T/e2")
    if ! cmp -s "$T/ref-$g" "$T/o2" || grep -q "VERIFY MISMATCH" "$T/e2" || [ "${bad:-1}" != 0 ] || ! [ "${ok:-0}" -gt 0 ]; then
        grep "VERIFY MISMATCH" "$T/e2" | head -5; fail "$g (verify: ${ok:-?} equal, ${bad:-?} different)"; continue
    fi
    ENVX= run "$D" "$T/g_$g" "$T/o3" "$T/e3"
    ld=$(num "blocks loaded" "$T/e3"); [ -n "$ld" ] || ld=$(sed -n 's/.*pemu jit cache: \([0-9][0-9]*\) blocks loaded.*/\1/p' "$T/e3")
    if cmp -s "$T/ref-$g" "$T/o3" && [ "${ld:-0}" -gt 0 ]; then echo "jit_cache $g: ok ($ok blocks verified, $ld loaded)"; else fail "$g (load: ${ld:-0} blocks loaded)"; fi
done

# 4. another program at the same addresses: the cache of alu must not hand its blocks to muldiv (static non-PIE guests share their link addresses)
ENVX= run "$T/c-alu" "$T/g_muldiv" "$T/o" "$T/e"
if cmp -s "$T/ref-muldiv" "$T/o"; then echo "jit_cache: another program, same addresses, other bytes: ok"; else fail "foreign blocks (output differs)"; fi
ENVX="PEMU_JIT_CACHE_VERIFY=1" run "$T/c-alu" "$T/g_muldiv" "$T/o" "$T/e"
if cmp -s "$T/ref-muldiv" "$T/o" && ! grep -q "VERIFY MISMATCH" "$T/e"; then echo "jit_cache: foreign blocks pass verify: ok"; else fail "foreign blocks (verify)"; fi

# 5. damage: garbage in the middle, a torn tail, a file of another format, a truncated header
F=$(ls "$T"/c-alu/*.pjc); SZ=$(wc -c < "$F")
D="$T/c-dmg"; newdir "$D"; cp "$F" "$D/f"; N=$D/$(basename "$F")
mv "$D/f" "$N"; chmod 600 "$N"
dd if=/dev/urandom of="$N" bs=1 count=300 seek=$((SZ / 2)) conv=notrunc 2>/dev/null          # 300 bytes of noise in the middle
head -c 100 /dev/urandom >> "$N"                                                              # and a torn tail
REC=$(num recorded "$T/e1")
ENVX= run "$D" "$T/g_alu" "$T/o" "$T/e"                                                       # most records survive: the reader skips the damage and goes on
LD=$(sed -n 's/.*pemu jit cache: \([0-9][0-9]*\) blocks loaded.*/\1/p' "$T/e"); TR=$(sed -n 's/.*blocks loaded.*, \([0-9][0-9]*\) translated.*/\1/p' "$T/e")
if cmp -s "$T/ref-alu" "$T/o" && [ "${LD:-0}" -gt 0 ]; then echo "jit_cache: damaged file (noise + torn tail): ok ($LD blocks loaded, $TR translated again)"; else fail "damaged file (${LD:-0} loaded)"; fi
ENVX="PEMU_JIT_CACHE_VERIFY=1" run "$D" "$T/g_alu" "$T/o" "$T/e"
if cmp -s "$T/ref-alu" "$T/o" && ! grep -q "VERIFY MISMATCH" "$T/e"; then echo "jit_cache: damaged file passes verify: ok"; else fail "damaged file (verify)"; fi
head -c 40 /dev/urandom > "$N"; chmod 600 "$N"
ENVX= run "$D" "$T/g_alu" "$T/o" "$T/e"
if cmp -s "$T/ref-alu" "$T/o"; then echo "jit_cache: file shorter than its header: ok"; else fail "short file"; fi
head -c 4096 /dev/urandom > "$N"; chmod 600 "$N"
ENVX= run "$D" "$T/g_alu" "$T/o" "$T/e"
if cmp -s "$T/ref-alu" "$T/o"; then echo "jit_cache: file of another format: ok"; else fail "foreign file"; fi

# unsafe directory (writable by others) and a relative path: ignored, nothing written, same output
D="$T/c-open"; mkdir -p "$D"; chmod 777 "$D"
ENVX= run "$D" "$T/g_alu" "$T/o" "$T/e"
if cmp -s "$T/ref-alu" "$T/o" && [ -z "$(ls "$D")" ]; then echo "jit_cache: directory others can write to is not used: ok"; else fail "unsafe directory"; fi
ENVX= run "relative/dir" "$T/g_alu" "$T/o" "$T/e"
if cmp -s "$T/ref-alu" "$T/o" && [ ! -e relative ]; then echo "jit_cache: relative path is not used: ok"; else fail "relative path"; fi
# a file readable by others is not trusted either
D="$T/c-mode"; newdir "$D"; ENVX= run "$D" "$T/g_alu" "$T/o" "$T/e"; chmod 644 "$D"/*.pjc
ENVX= run "$D" "$T/g_alu" "$T/o" "$T/e"
if cmp -s "$T/ref-alu" "$T/o" && [ "$(num "blocks loaded" "$T/e")" = 0 ] 2>/dev/null; then echo "jit_cache: file with open permissions is not loaded: ok"; else fail "file permissions"; fi

# 6. four processes of the same program fill the same cache at once
D="$T/c-par"; newdir "$D"
for i in 1 2 3 4; do ( ENVX= run "$D" "$T/g_ext" "$T/po$i" "$T/pe$i" ) & done; wait
okp=1; for i in 1 2 3 4; do cmp -s "$T/ref-ext" "$T/po$i" || okp=0; done
ENVX="PEMU_JIT_CACHE_VERIFY=1" run "$D" "$T/g_ext" "$T/o" "$T/e"
if [ $okp = 1 ] && cmp -s "$T/ref-ext" "$T/o" && ! grep -q "VERIFY MISMATCH" "$T/e" && [ "$(num equal "$T/e")" -gt 0 ]; then echo "jit_cache: 4 processes writing one cache: ok"; else fail "parallel writers"; fi

[ $rc = 0 ] && echo "jit_cache: all ok"
exit $rc

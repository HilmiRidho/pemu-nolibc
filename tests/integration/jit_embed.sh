#!/bin/sh
# fuse: cmp/test/sub/add/and + jcc over every condition, operand form and size, with the flags dead or live at the exits (tests/guests/jit_fuse.c, 19200 cases);
# vec: palignr, pmovmskb, the pxor zero idiom (tests/guests/jit_vec.c);
# inplace: shl/shr/sar/rol/ror by an immediate and imul on pinned registers with the flags dead (tests/guests/jit_inplace_gen.py, ~23000 cases), one host instruction each.
# pin: guest r8..r15 pinned to host registers: ~9000 generated cases (tests/guests/jit_pin_gen.py, needs python3) with every register live and different, native vs interpreter vs JIT.
# JIT tests for translations that have no AArch64 hardware in the loop: the 66 0F 38 instructions (tests/guests/jit_sse4_*.c), imul / rol / ror with live flags (tests/guests/jit_alu.c)
# cvttss2si / cvttsd2si (tests/guests/jit_cvt.c) pmuludq (tests/guests/jit_pmul.c) and packed float arithmetic (tests/guests/jit_pfp.c).
# Each guest is run natively, under the interpreter (-nojit) and under the AArch64 JIT, whose generated code is executed by the interpreter of tests/oracle/aarch64/ (-DPEMU_JIT_EMBED,
# see tests/oracle/aarch64/embed_hook.inc.h). Interpreter and JIT must agree on every line; the native run only on the lines without a leading '~' (flags that the SDM calls undefined).
# x86_64 host with gcc and a static libc.  usage: tests/integration/jit_embed.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = x86_64 ] || { echo "jit_embed: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
echo 'int main(void){return 0;}' > "$T/s.c"
$CC -static -o "$T/s" "$T/s.c" 2>/dev/null || { echo "jit_embed: no static libc, skipping"; exit 0; }
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_EMBED -c tests/oracle/aarch64/oracle.c -o "$T/a64.o"
$CC -O2 -w -DPEMU_UNIT_TEST_LIBC -DPEMU_JIT_EMBED -c src/pemu.c -o "$T/x64.o"
$CC -o "$T/pemu_embed" "$T/x64.o" "$T/a64.o" -lm -lpthread
rc=0
for g in sse4_reg sse4_mem alu muldiv ext cvt fuse vec pmul pfp pin inplace hc hci ro shuf seg; do
    case $g in sse4_*|vec|pmul|pfp|shuf) f=tests/guests/jit_$g.c; fl=-msse4.2 ;; hci) f=tests/guests/jit_$g.c; fl="-msse4.2" ;; ro|seg) f=tests/guests/jit_$g.c; fl=-mno-red-zone ;; *) f=tests/guests/jit_$g.c; fl= ;; esac
    if [ $g = pin ]; then
        command -v python3 >/dev/null || { echo "jit_embed pin: python3 missing, skipping"; continue; }
        python3 tests/guests/jit_pin_gen.py > "$T/jit_pin.c"; f=$T/jit_pin.c; fl=-no-pie     # fixed addresses: pointers in registers print the same natively and emulated
    fi
    if [ $g = inplace ]; then
        command -v python3 >/dev/null || { echo "jit_embed inplace: python3 missing, skipping"; continue; }
        python3 tests/guests/jit_inplace_gen.py > "$T/jit_inplace.c"; f=$T/jit_inplace.c       # shl/shr/sar/rol/ror by imm and imul on pinned registers, flags dead: translated to one host instruction
    fi
    $CC -O1 -w $fl -static -o "$T/g_$g" "$f" -lm
    "$T/g_$g" | grep -v '^~' > "$T/native-$g"
    timeout 120 "$T/pemu_embed" -nojit "$T/g_$g" > "$T/nojit-$g" 2>&1 || true
    PEMU_JIT_STATS=1 timeout 300 "$T/pemu_embed" "$T/g_$g" > "$T/jit-$g" 2> "$T/stats-$g" || true
    if diff -u "$T/nojit-$g" "$T/jit-$g" > "$T/diff"; then echo "jit_embed $g (jit = interpreter): ok"; else cat "$T/diff"; echo "jit_embed $g (jit = interpreter): FAILED"; rc=1; fi
    for v in nojit jit; do
        grep -v '^~' "$T/$v-$g" > "$T/$v-$g.def"
        if diff -u "$T/native-$g" "$T/$v-$g.def" > "$T/diff"; then echo "jit_embed $g ($v = native): ok"; else cat "$T/diff"; echo "jit_embed $g ($v = native): FAILED"; rc=1; fi
    done
done
# what the JIT translates must not run through the interpreter any more (the register-operand guest uses no memory form, so even pmovsxbq/pmovzxbq are translated)
if ! grep -q 'third opcode byte, executed fallbacks' "$T/stats-sse4_reg" && ! grep -q 'interpreter fallbacks executed' "$T/stats-sse4_reg"; then
    echo "jit_embed: no per-opcode fallback statistics in the JIT output: FAILED"; rc=1
elif grep -E 'executed fallbacks.* 38/(17|1c|1d|1e|2[0-5]|29|2b|3[0-5]|3[7-9a-f]|40) x' "$T/stats-sse4_reg" > /dev/null; then
    grep 'executed fallbacks' "$T/stats-sse4_reg"; echo "jit_embed: a translated 0F 38 instruction still falls back: FAILED"; rc=1
else echo "jit_embed: translated 0F 38 instructions no longer fall back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-alu"; then echo "jit_embed: no fallback statistics for the alu guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-alu" | grep -E ' (0f af|69|6b|c1|d1) x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-alu"; echo "jit_embed: imul / rol / ror still fall back: FAILED"; rc=1
else echo "jit_embed: imul / rol / ror with live flags no longer fall back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-ext"; then echo "jit_embed: no fallback statistics for the ext guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-ext" | grep -E ' (98|99|69|6b|9c|9d) x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-ext"; echo "jit_embed: cbw / cwd / imul r16, imm / pushf / popf still fall back: FAILED"; rc=1
else echo "jit_embed: cbw / cwd / imul r16, imm / pushf / popf no longer fall back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-cvt"; then echo "jit_embed: no fallback statistics for the cvt guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-cvt" | grep -E ' 0f 2c x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-cvt"; echo "jit_embed: cvttss2si / cvttsd2si still fall back: FAILED"; rc=1
else echo "jit_embed: cvttss2si / cvttsd2si no longer fall back: ok"; fi
if ! grep -q 'flag producers fused' "$T/stats-fuse" || grep -q ' 0 flag producers fused' "$T/stats-fuse"; then echo "jit_embed: no flag producer was fused in the fuse guest: FAILED"; rc=1; else echo "jit_embed: cmp/test/sub/add + jcc are fused: ok"; fi
if grep 'interpreter fallbacks executed' "$T/stats-vec" | grep -E ' 0f 3a x' > /dev/null; then echo "jit_embed: palignr still falls back: FAILED"; rc=1; else echo "jit_embed: palignr no longer falls back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-pmul" && ! grep -q 'no translated instruction falls back' "$T/stats-pmul"; then echo "jit_embed: no fallback statistics for the pmul guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-pmul" | grep -E ' 0f f4 x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-pmul"; echo "jit_embed: pmuludq still falls back: FAILED"; rc=1
else echo "jit_embed: pmuludq no longer falls back: ok"; fi
if ! grep -q 'interpreter fallbacks executed' "$T/stats-pfp"; then echo "jit_embed: no fallback statistics for the pfp guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-pfp" | grep -E ' 0f (14|15|51|58|59|5c|5e) x' > /dev/null; then
    grep 'interpreter fallbacks executed' "$T/stats-pfp"; echo "jit_embed: packed float arithmetic still falls back: FAILED"; rc=1
else echo "jit_embed: packed float arithmetic no longer falls back: ok"; fi
# the helper-call translations (cpuid, rdtsc, xgetbv, string instructions, shld / shrd, locked bt*, narrow adc / sbb / rotates, mul / div 8 and 16 bit, the SSE / x87 long tail)
for g in hc hci; do
    if ! grep -q 'interpreter fallbacks executed' "$T/stats-$g"; then echo "jit_embed: no fallback statistics for the $g guest: FAILED"; rc=1
    elif grep 'interpreter fallbacks executed' "$T/stats-$g" | grep -E ' (0f a2|0f 31|0f 01|a4|a5|a6|a7|aa|ab|ac|ad|ae|af|0f a4|0f a5|0f ac|0f ad|0f ae|0f ab|0f b3|0f bb|0f ba|0f c2|0f c6|0f 70|0f e6|0f 3a|0f 38|0f 5a|0f 5b|0f 7c|0f 7d|0f d0|0f 50|0f 2d|0f 12|0f 16|0f f0|0f e4|0f e5|0f f5|0f f6|d8|d9|da|db|dc|dd|de|df|80|10|11|12|13|14|18|19|1c|1d|d0|d2|66 d3) x' > /dev/null; then
        grep 'interpreter fallbacks executed' "$T/stats-$g"; echo "jit_embed: a helper-call translation ($g) still falls back: FAILED"; rc=1
    else echo "jit_embed: $g instructions are translated (only syscall / hlt / pushf / idiv-by-register may still run in the interpreter): ok"; fi
done
# pop r/m64, mov r/m16,Sreg and mov Sreg,r/m16 are on the hot path of Wine's system call dispatcher: they must be translated. The one 8f left is pop [rsp+disp] (the address is formed after rsp moved), which the guest uses on purpose.
if ! grep -q 'interpreter fallbacks executed' "$T/stats-seg"; then echo "jit_embed: no fallback statistics for the seg guest: FAILED"; rc=1
elif grep 'interpreter fallbacks executed' "$T/stats-seg" | grep -E ' (8c|8e) x' > /dev/null || [ "$(grep 'interpreter fallbacks executed' "$T/stats-seg" | grep -oE ' 8f x[0-9]+' | sed 's/.* x//' | head -1 | grep . || echo 0)" -gt 1 ]; then
    grep 'interpreter fallbacks executed' "$T/stats-seg"; echo "jit_embed: pop r/m64 / mov Sreg still fall back: FAILED"; rc=1
else echo "jit_embed: pop r/m64 / mov r/m16,Sreg / mov Sreg,r/m16 no longer fall back: ok"; fi
exit $rc

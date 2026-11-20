#!/bin/sh
# x87 behaviour (tests/guests/x87_guest.c): the guest is run natively and under pemu (interpreter); every output line must be identical.
# Covers: denormals stored as 80-bit, fprem/fprem1 remainder + quotient bits, precision control 24/53/64, tag word (fnstenv, fxsave), ffreep and the 8087 nops.
# Also: fpatan under all rounding modes, an addition / subtraction with a denormal operand under every exception mask, rounding control (down / up / toward zero) with overflow, denormal results and exact zeros, an unmasked IE / ZE / DE leaving the stack alone, scaled results for an unmasked OE / UE, stores that must not happen, exception flags (IE DE ZE OE UE PE SF, C1), sin/cos/tan with the CPU's 66-bit pi, SIGFPE for unmasked exceptions, random + - * / under all precision controls.
# x86_64 host with gcc and a static libc.  usage: tests/integration/x87.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-build}
[ "$(uname -m)" = x86_64 ] || { echo "x87: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
$CC -O1 -static -o "$T/g" tests/guests/x87_guest.c 2>/dev/null || { echo "x87: no static libc, skipping"; exit 0; }
"$T/g" > "$T/native"
timeout 120 "$OUT/pemu" "$T/g" > "$T/pemu" 2>&1 || true
if diff -u "$T/native" "$T/pemu"; then echo "x87 (pemu = native, $(wc -l < "$T/native") lines): ok"; else echo "x87: FAILED"; exit 1; fi

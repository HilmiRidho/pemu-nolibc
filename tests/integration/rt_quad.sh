#!/bin/sh
# host/rt/rt_quad.h (the binary128 soft-float of the aarch64 libc-free build), run on an x86_64 machine where the type is __float128:
#  - nearest mode against libquadmath: sqrt, fmod, remainder, nextafter, ldexp, frexp, trunc/floor/ceil/round/rint, fma bit for bit; expm1 and atan2 within 6 ulp
#    (where sqrt / fma / ldexp differ from libquadmath, the exact oracle below decides who is right: libquadmath's own rounding is not assumed correct)
#  - all four rounding modes against an exact Fraction oracle: fma, sqrt, ldexp (tests/unit/rt_quad_oracle.py)
# Needs gcc, libquadmath and python3; skips (exit 0) when one is missing, unless PEMU_REQUIRE_TESTS=1 (then exit 1).  Not an aarch64 run: the only aarch64 asm (FPCR / FPSR) is in rt_math.h.
# PEMU_REQUIRE_TESTS=1 (set by CI): a missing tool is a failure instead of a skip, so a test that did not run cannot be mistaken for one that passed.
set -e
skip() { echo "rt_quad: $1, skipping"; [ -z "$PEMU_REQUIRE_TESTS" ] || { echo "rt_quad: FAILED (PEMU_REQUIRE_TESTS is set, a skip is not allowed)"; exit 1; }; exit 0; }
[ "$(uname -m)" = x86_64 ] || { echo "rt_quad: x86_64 only, skipping"; exit 0; }
CC=${CC:-gcc}; D=$(mktemp -d); trap 'rm -rf "$D"' EXIT; T=$(cd "$(dirname "$0")/.." && pwd)
echo 'int main(void){return 0;}' | $CC -x c - -lquadmath -o "$D/probe" 2>/dev/null || skip "no libquadmath"
command -v python3 >/dev/null 2>&1 || skip "no python3"
# every step must succeed on its own: a test program that does not compile or run, or that prints no vectors, is a failure (exit 1), never a quiet pass
$CC -O2 -w -o "$D/t" "$T/unit/rt_quad_test.c" -lquadmath || { echo "rt_quad: FAILED, tests/unit/rt_quad_test.c did not compile"; exit 1; }
# sqrt / fma / ldexp that libquadmath answers differently are judged by the exact oracle (see rt_quad_test.c): only an rt_quad that is itself wrong fails
"$D/t" -m "$D/arb" || { echo "rt_quad: FAILED, the nearest-mode comparison against libquadmath"; exit 1; }
if [ -s "$D/arb" ]; then
  python3 "$T/unit/rt_quad_oracle.py" < "$D/arb" || { echo "rt_quad: FAILED, rt_quad differs from libquadmath AND from the exact result (the vectors above)"; exit 1; }
  echo "rt_quad: note: libquadmath's own sqrt / fma / ldexp differ from the exact result in those cases; rt_quad matches the exact result"
fi
"$D/t" -d > "$D/vectors" || { echo "rt_quad: FAILED, the vector dump (-d)"; exit 1; }
[ -s "$D/vectors" ] || { echo "rt_quad: FAILED, the test program wrote no vectors for the oracle"; exit 1; }
python3 "$T/unit/rt_quad_oracle.py" < "$D/vectors" || { echo "rt_quad: FAILED, the rounding-mode oracle"; exit 1; }

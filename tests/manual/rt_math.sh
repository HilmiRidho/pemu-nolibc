#!/bin/sh
# host/rt/rt_math.h against the host libm (x86_64 only; needs glibc libm and objcopy).
# nextafterl must be bit-identical, pseudo-denormals and unnormals included; expm1l may differ by up to 2.5 ulp
# (glibc itself is off by up to ~2.2 ulp here, rt_math is closer to the exact value).  Not run by build.sh.
set -e
[ "$(uname -m)" = x86_64 ] || { echo "rt_math: x86_64 only, skipping"; exit 0; }
CC=${CC:-gcc}; D=$(mktemp -d); trap 'rm -rf "$D"' EXIT; R=$(cd "$(dirname "$0")/../.." && pwd)
printf '#include "%s/host/rt/rt_math.h"\n' "$R" > "$D/rtm.c"
$CC -O2 -fno-builtin -fno-stack-protector -w -c "$D/rtm.c" -o "$D/rtm.o"
objcopy --prefix-symbols=rt_ "$D/rtm.o" "$D/rtm_p.o"
cat > "$D/t.c" <<'C'
#include <math.h>
#include <stdio.h>
#include <string.h>
long double rt_nextafterl(long double, long double), rt_expm1l(long double);
static unsigned long long s = 88172645463325252ULL;
static unsigned long long rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static long double mk(unsigned long long m, unsigned short se) { long double d = 0; memcpy(&d, &m, 8); memcpy((char *) &d + 8, &se, 2); return d; }
int main(void) {
    long bad = 0; double worst = 0;
    for (int i = 0; i < 1000000; i++) {
        unsigned short se = (unsigned short) (rnd() & 0x8000) | (unsigned short) (rnd() % 3 == 0 ? rnd() % 4 : rnd() & 0x7fff);
        unsigned long long m = rnd(); if (rnd() % 2) m |= 1ULL << 63; if (rnd() % 7 == 0) m = rnd() % 2 ? 1ULL << 63 : ~0ULL;
        volatile long double x = mk(m, se), y = (rnd() % 3 == 0) ? INFINITY : (rnd() % 2 ? -INFINITY : 0.0L);
        long double a = rt_nextafterl(x, y), b = nextafterl(x, y);
        if (a != a && b != b) continue;
        if (memcmp(&a, &b, 10)) bad++;
    }
    for (int i = 0; i < 300000; i++) {
        long double x = ((long double) (rnd() % 2000001) - 1000000) / 1000000.0L * (i % 2 ? 2 : 50);
        long double a = rt_expm1l(x), b = expm1l(x); if (b == 0 || !isfinite(b)) continue;
        int e; frexpl(b, &e); double u = (double) (fabsl(a - b) / ldexpl(1.0L, e - 64)); if (u > worst) worst = u;
    }
    printf("rt_math: nextafterl mismatches %ld, expm1l worst %.2f ulp vs glibc\n", bad, worst);
    return bad != 0 || worst > 2.5;
}
C
$CC -O1 -w "$D/t.c" "$D/rtm_p.o" -lm -o "$D/t"
"$D/t"

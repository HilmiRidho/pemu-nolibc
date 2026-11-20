/* host/rt/rt_quad.h against libquadmath (nearest mode) and, for the directed modes, an exact oracle in tests/unit/rt_quad_oracle.py (vectors on stdout with -d). x86_64, gcc + libquadmath. */
#include <quadmath.h>
#include <stdio.h>
#include <string.h>
#include "../../host/rt/rt_quad.h"
static rt_u64 s = 88172645463325252ULL;
static rt_u64 rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static rt_q mk(void) {                                   /* random bit patterns, biased to the interesting corners */
    rt_u128 m = ((rt_u128) rnd() << 64) | rnd(); unsigned k = (unsigned) (rnd() % 12); unsigned e;
    switch (k) { case 0: e = (unsigned) (rnd() % 4); break; case 1: e = 0x7ffe - (unsigned) (rnd() % 3); break; case 2: e = 0x7fff; break;
                 default: e = 16383 - 130 + (unsigned) (rnd() % 260); }
    if (rnd() % 9 == 0) m &= ((rt_u128) 1 << (rnd() % 112)) - 1;
    if (rnd() % 13 == 0) m = 0;
    m &= RTQ_MASK112; m |= (rt_u128) e << 112; if (rnd() & 1) m |= RTQ_SIGN;
    return rt_q_from(m);
}
static int same(rt_q a, rt_q b) { if (isnanq(a) && isnanq(b)) return 1; return rt_q_bits(a) == rt_q_bits(b); }
static double ulps(rt_q a, rt_q b) {
    if (a == b) return 0; if (!finiteq(a) || !finiteq(b)) return 1e30;
    int e; frexpq(b, &e); e -= 113; if (e < -16494) e = -16494; return (double) (fabsq(a - b) / ldexpq(1, e));
}
static void rpt(const char *op, rt_q a, rt_q b, rt_q got, rt_q want) {   /* a mismatch with the raw bits: a decimal print of a long double hides the very bits that differ */
    #define H(x) (unsigned long long) (rt_q_bits(x) >> 64), (unsigned long long) rt_q_bits(x)
    printf("%s: a=%016llx%016llx b=%016llx%016llx got=%016llx%016llx libquadmath=%016llx%016llx\n", op, H(a), H(b), H(got), H(want));
    #undef H
}
/* sqrt / fma / ldexp are exact, so for them "libquadmath disagrees" does not say who is wrong (libquadmath's own sqrtq / fmaq have had rounding bugs in some
 * releases).  With -m FILE every such disagreement on finite operands is written as a vector (nearest mode, rt_quad's result) for tests/unit/rt_quad_oracle.py,
 * which recomputes it exactly: rt_quad wrong -> the oracle fails; rt_quad right -> libquadmath is the one that differs (reported, not a failure).
 * Without -m, or for NaN / infinity operands the oracle does not judge, a disagreement counts as a failure as before. */
static FILE *arbf; static long narb;
static int judge(const char *op, rt_q a, rt_q b, rt_q c, rt_q r, rt_q e) {
    if (!arbf) return 0;
    int ok = !strcmp(op, "sqrt") ? finiteq(a) && !(a < 0) : !strcmp(op, "fma") ? finiteq(a) && finiteq(b) && finiteq(c) : finiteq(a) && a != 0;
    if (!ok) return 0;
    #define H(x) (unsigned long long) (rt_q_bits(x) >> 64), (unsigned long long) rt_q_bits(x)
    fprintf(arbf, "%s 0 %016llx%016llx %016llx%016llx %016llx%016llx %016llx%016llx\n", op, H(a), H(b), H(c), H(r));
    #undef H
    if (narb++ < 5) { printf("%s differs from libquadmath (the exact oracle decides): ", op); rpt(op, a, b, r, e); }
    return 1;
}
static void dump(const char *op, int mode, rt_q a, rt_q b, rt_q c, rt_q r) {
    printf("%s %d %032llx%016llx %032llx%016llx %032llx%016llx %032llx%016llx\n", op, mode, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL); (void) a; (void) b; (void) c; (void) r;
}
int main(int argc, char **argv) {
    int dumpmode = argc > 1 && !strcmp(argv[1], "-d");
    if (argc > 2 && !strcmp(argv[1], "-m") && !(arbf = fopen(argv[2], "w"))) { printf("rt_quad_test: cannot write %s\n", argv[2]); return 1; }
    long bad = 0; double worst_e = 0, worst_a = 0;
    if (dumpmode) {                                      /* op mode a b c result, 32 hex digits each */
        for (int i = 0; i < 60000; i++) {
            rt_q a = mk(), b = mk(), c = mk(); int mode = (int) (rnd() % 4);
            if (rnd() % 3 == 0) { c = -a * 0; c = rt_q_from(rt_q_bits(c)); }
            #define P(op, m, A, B, C, R) printf("%s %d %016llx%016llx %016llx%016llx %016llx%016llx %016llx%016llx\n", op, m, (rt_u64) (rt_q_bits(A) >> 64), (rt_u64) rt_q_bits(A), (rt_u64) (rt_q_bits(B) >> 64), (rt_u64) rt_q_bits(B), (rt_u64) (rt_q_bits(C) >> 64), (rt_u64) rt_q_bits(C), (rt_u64) (rt_q_bits(R) >> 64), (rt_u64) rt_q_bits(R))
            P("fma", mode, a, b, c, rt_q_fma(a, b, c, mode));
            P("sqrt", mode, a, 0, 0, rt_q_sqrt(a, mode));
            int n = (int) (rnd() % 40000) - 20000;
            P("ldexp", mode, a, rt_q_from((rt_u128) (unsigned) n), 0, rt_q_ldexp(a, n, mode));
            if (rnd() % 3 == 0) { rt_q m = rt_q_from(((rt_u128) (16383 + 20 + rnd() % 80) << 112) | (((rt_u128) rnd() << 64 | rnd()) & RTQ_MASK112)); rt_q b2 = mk(); P("fma", mode, m, b2, c, rt_q_fma(m, b2, c, mode)); }
        }
        (void) dump; return 0;
    }
    for (int i = 0; i < 100000; i++) {
        rt_q a = mk(), b = mk(), c = mk(), r, e; int ea, eb, n = (int) (rnd() % 40000) - 20000;
        if (i % 3 == 0 && finiteq(a)) { b = a * (rt_q) 1.0000000001L; }     /* close operands: fmod / remainder / cancellation */
        if (i % 5 == 0) c = -a * b;
        r = rt_q_sqrt(a, 0); e = sqrtq(a); if (!same(r, e) && !judge("sqrt", a, 0, 0, r, e)) { if (bad++ < 10) rpt("sqrt", a, b, r, e); }
        r = rt_q_fmod(a, b, 0); e = fmodq(a, b); if (!same(r, e)) { if (bad++ < 10) rpt("fmod", a, b, r, e); }
        r = rt_q_fmod(a, b, 1); e = remainderq(a, b); if (!same(r, e)) { if (bad++ < 10) rpt("remainder", a, b, r, e); }
        r = rt_q_nextafter(a, b); e = nextafterq(a, b); if (!same(r, e)) { if (bad++ < 10) rpt("nextafter", a, b, r, e); }
        r = rt_q_ldexp(a, n, 0); e = ldexpq(a, n); if (!same(r, e) && !judge("ldexp", a, rt_q_from((rt_u128) (unsigned) n), 0, r, e)) { if (bad++ < 10) rpt("ldexp", a, b, r, e); }
        r = rt_q_frexp(a, &ea); e = frexpq(a, &eb); if (!same(r, e) || (finiteq(a) && ea != eb)) { if (bad++ < 10) rpt("frexp", a, b, r, e); }
        r = rt_q_round(a, 3); e = truncq(a); if (!same(r, e)) { if (bad++ < 10) rpt("trunc", a, b, r, e); }
        r = rt_q_round(a, 1); e = floorq(a); if (!same(r, e)) { if (bad++ < 10) rpt("floor", a, b, r, e); }
        r = rt_q_round(a, 2); e = ceilq(a); if (!same(r, e)) { if (bad++ < 10) rpt("ceil", a, b, r, e); }
        r = rt_q_round(a, 4); e = roundq(a); if (!same(r, e)) { if (bad++ < 10) rpt("round", a, b, r, e); }
        r = rt_q_round(a, 0); e = nearbyintq(a); if (!same(r, e)) { if (bad++ < 10) rpt("rint", a, b, r, e); }
        r = rt_q_fma(a, b, c, 0); e = fmaq(a, b, c); if (!same(r, e) && !judge("fma", a, b, c, r, e)) { if (bad++ < 10) rpt("fma", a, b, r, e); }
    }
    for (int i = 0; i < 100000; i++) {
        rt_q x = ((rt_q) ((long) (rnd() % 2000001) - 1000000) / 1000000) * (i % 3 == 0 ? 3 : i % 3 == 1 ? 60 : 11000);
        rt_q r = rt_q_expm1(x, 0), e = expm1q(x); double u = ulps(r, e); if (u > worst_e) { worst_e = u; if (u > 6) printf("expm1 x=%.20Lg %.1f\n", (long double) x, u); }
        rt_q y = mk(), z = mk(); if (i % 2) { y = x; z = ((rt_q) (long) (rnd() % 2001) - 1000) / 7; }
        r = rt_q_atan2(y, z); e = atan2q(y, z); if (isnanq(e) != isnanq(r)) bad++; else if (!isnanq(e)) { u = ulps(r, e); if (u > worst_a) { worst_a = u; if (u > 6) printf("atan2 %.20Lg %.20Lg %.1f\n", (long double) y, (long double) z, u); } }
    }
    if (arbf) { fclose(arbf); if (narb) printf("rt_quad: %ld sqrt / fma / ldexp results differ from libquadmath, written for the exact oracle\n", narb); }
    printf("rt_quad: mismatches %ld, expm1 worst %.2f ulp, atan2 worst %.2f ulp (vs libquadmath)\n", bad, worst_e, worst_a);
    return bad != 0 || worst_e > 6 || worst_a > 6;
}

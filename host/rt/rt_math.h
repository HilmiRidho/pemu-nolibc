/* host/rt/math.h - the libm that pemu uses: rounding, fma (exact, any rounding mode), long double, fenv.  SPDX-License-Identifier: Apache-2.0
 * x86_64: long double is the x87 80-bit type, done with the x87 itself.  aarch64: long double is binary128, done by the integer soft-float
 * of rt_quad.h (checked on x86_64 by tests/integration/rt_quad.sh); fenv reads and writes FPCR / FPSR.  The aarch64 asm here has not run on real hardware. */
#ifndef PEMU_RT_MATH_H
#define PEMU_RT_MATH_H
#include <fenv.h>
#include <float.h>
#include <math.h>
#if !defined(__x86_64__) && !defined(__aarch64__)
#error "host/rt/math.h: only x86_64 and aarch64 hosts are covered; build with libc on other hosts"
#endif
typedef unsigned long long rt_u64;
#if defined(__aarch64__)
#include "rt_quad.h"
#endif

#if defined(__aarch64__)
/* ---- fenv: rounding mode in FPCR bits 23:22 (the FE_* values are already shifted), exception flags in FPSR bits 0..4 */
static inline unsigned long rt_rd_fpcr(void) { unsigned long v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline unsigned long rt_rd_fpsr(void) { unsigned long v; __asm__ volatile("mrs %0, fpsr" : "=r"(v)); return v; }
static inline void rt_wr_fpcr(unsigned long v) { __asm__ volatile("msr fpcr, %0" :: "r"(v)); }
static inline void rt_wr_fpsr(unsigned long v) { __asm__ volatile("msr fpsr, %0" :: "r"(v)); }
int fegetround(void) { return (int) (rt_rd_fpcr() & 0xc00000UL); }
int fesetround(int r) { if (r & ~0xc00000) return 1; rt_wr_fpcr((rt_rd_fpcr() & ~0xc00000UL) | (unsigned long) r); return 0; }
static int rt_mxmode(void) { switch ((rt_rd_fpcr() >> 22) & 3) { case 0: return 0; case 1: return 2; case 2: return 1; default: return 3; } }  /* 0 near, 1 down, 2 up, 3 zero */
int fetestexcept(int m) { return (int) (rt_rd_fpsr() & 0x1f & (unsigned long) m); }
int feclearexcept(int m) { rt_wr_fpsr(rt_rd_fpsr() & ~((unsigned long) m & 0x1f)); return 0; }
int feraiseexcept(int m) { rt_wr_fpsr(rt_rd_fpsr() | ((unsigned long) m & 0x1f)); return 0; }
#else
/* ---- fenv: the rounding mode lives in the x87 control word and in MXCSR, both are kept equal like glibc does */
int fegetround(void) { unsigned short cw; __asm__ volatile("fnstcw %0" : "=m"(cw)); return cw & 0xc00; }
int fesetround(int r) {
    if (r & ~0xc00) return 1;
    unsigned short cw; unsigned mx; __asm__ volatile("fnstcw %0" : "=m"(cw)); __asm__ volatile("stmxcsr %0" : "=m"(mx));
    cw = (unsigned short) ((cw & ~0xc00) | r); mx = (mx & ~0x6000u) | ((unsigned) r << 3);
    __asm__ volatile("fldcw %0" :: "m"(cw)); __asm__ volatile("ldmxcsr %0" :: "m"(mx));
    return 0;
}
static int rt_mxmode(void) { unsigned mx; __asm__ volatile("stmxcsr %0" : "=m"(mx)); switch ((mx >> 13) & 3) { case 0: return 0; case 1: return 1; case 2: return 2; default: return 3; } }  /* 0 near, 1 down, 2 up, 3 zero */
int fetestexcept(int m) { unsigned mx; unsigned short sw; __asm__ volatile("stmxcsr %0" : "=m"(mx)); __asm__ volatile("fnstsw %0" : "=m"(sw)); return (int) ((mx | sw) & 0x3f & (unsigned) m); }
int feclearexcept(int m) {
    unsigned mx; __asm__ volatile("stmxcsr %0" : "=m"(mx)); mx &= ~((unsigned) m & 0x3f); __asm__ volatile("ldmxcsr %0" :: "m"(mx));
    char env[28]; __asm__ volatile("fnstenv %0" : "=m"(env)); *(unsigned short *) (env + 4) &= (unsigned short) ~((unsigned) m & 0x3f); __asm__ volatile("fldenv %0" :: "m"(env)); return 0;
}
int feraiseexcept(int m) { unsigned mx; __asm__ volatile("stmxcsr %0" : "=m"(mx)); mx |= (unsigned) m & 0x3f; __asm__ volatile("ldmxcsr %0" :: "m"(mx)); return 0; }
#endif

/* ---- double / float */
double fabs(double x) { union { double d; rt_u64 u; } v = { x }; v.u &= ~(1ULL << 63); return v.d; }
float fabsf(float x) { union { float d; unsigned u; } v = { x }; v.u &= 0x7fffffffu; return v.d; }
double copysign(double x, double y) { union { double d; rt_u64 u; } a = { x }, b = { y }; a.u = (a.u & ~(1ULL << 63)) | (b.u & (1ULL << 63)); return a.d; }
#if defined(__aarch64__)
double sqrt(double x) { double r; __asm__("fsqrt %d0, %d1" : "=w"(r) : "w"(x)); return r; }
float sqrtf(float x) { float r; __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x)); return r; }
#else
double sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
float sqrtf(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
#endif
double trunc(double x) { if (!(fabs(x) < 4503599627370496.0)) return x; double t = (double) (long long) x; return t == 0 ? copysign(0.0, x) : t; }
double floor(double x) { if (!(fabs(x) < 4503599627370496.0)) return x; double t = (double) (long long) x; if (t > x) t -= 1; return t == 0 ? copysign(0.0, x) : t; }
double ceil(double x) { if (!(fabs(x) < 4503599627370496.0)) return x; double t = (double) (long long) x; if (t < x) t += 1; return t == 0 ? copysign(0.0, x) : t; }
double nearbyint(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;
    volatile double m = copysign(4503599627370496.0, x), y = x + m; y = y - m;
    return y == 0 ? copysign(0.0, x) : y;
}
double rint(double x) { return nearbyint(x); }
double round(double x) { if (!(fabs(x) < 4503599627370496.0)) return x; double r = trunc(x); if (fabs(x - r) >= 0.5) r += copysign(1.0, x); return r == 0 ? copysign(0.0, x) : r; }
float truncf(float x) { return (float) trunc((double) x); }
float floorf(float x) { return (float) floor((double) x); }
float ceilf(float x) { return (float) ceil((double) x); }
float nearbyintf(float x) { return (float) nearbyint((double) x); }
float rintf(float x) { return nearbyintf(x); }
float roundf(float x) { return (float) round((double) x); }
double fmax(double a, double b) { if (a != a) return b; if (b != b) return a; return a > b ? a : b; }
double fmin(double a, double b) { if (a != a) return b; if (b != b) return a; return a < b ? a : b; }
float fmaxf(float a, float b) { if (a != a) return b; if (b != b) return a; return a > b ? a : b; }
float fminf(float a, float b) { if (a != a) return b; if (b != b) return a; return a < b ? a : b; }
long double fmaxl(long double a, long double b) { if (a != a) return b; if (b != b) return a; return a > b ? a : b; }
long double fminl(long double a, long double b) { if (a != a) return b; if (b != b) return a; return a < b ? a : b; }

#if defined(__aarch64__)
/* ---- long double = binary128 (rt_quad.h): the current rounding mode is read from FPCR for each call */
long double sqrtl(long double x) { return rt_q_sqrt(x, rt_mxmode()); }
long double fabsl(long double x) { return rt_q_fabs(x); }
long double copysignl(long double x, long double y) { return rt_q_copysign(x, y); }
long double nearbyintl(long double x) { int m = rt_mxmode(); return rt_q_round(x, m == 0 ? 0 : m); }   /* modes 1 down, 2 up, 3 zero are the kinds 1 floor, 2 ceil, 3 trunc */
long double rintl(long double x) { return nearbyintl(x); }
long double truncl(long double x) { return rt_q_round(x, 3); }
long double floorl(long double x) { return rt_q_round(x, 1); }
long double ceill(long double x) { return rt_q_round(x, 2); }
long double roundl(long double x) { return rt_q_round(x, 4); }
long double ldexpl(long double x, int n) { return rt_q_ldexp(x, n, rt_mxmode()); }
long double scalbnl(long double x, int n) { return ldexpl(x, n); }
long double frexpl(long double x, int *e) { return rt_q_frexp(x, e); }
long double fmodl(long double x, long double y) { return rt_q_fmod(x, y, 0); }
long double remainderl(long double x, long double y) { return rt_q_fmod(x, y, 1); }
long double atan2l(long double y, long double x) { return rt_q_atan2(y, x); }
long double nextafterl(long double x, long double y) { return rt_q_nextafter(x, y); }
long double expm1l(long double x) { return rt_q_expm1(x, rt_mxmode()); }
long double fmal(long double a, long double b, long double c) { return rt_q_fma(a, b, c, rt_mxmode()); }
double fma(double a, double b, double c) { double r; __asm__("fmadd %d0, %d1, %d2, %d3" : "=w"(r) : "w"(a), "w"(b), "w"(c)); return r; }
float fmaf(float a, float b, float c) { float r; __asm__("fmadd %s0, %s1, %s2, %s3" : "=w"(r) : "w"(a), "w"(b), "w"(c)); return r; }
#else
/* ---- long double = x87 extended */
union rt_ld { long double d; struct { rt_u64 m; unsigned short se; } b; };
long double sqrtl(long double x) { long double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
long double fabsl(long double x) { union rt_ld v = { x }; v.b.se &= 0x7fff; return v.d; }
long double copysignl(long double x, long double y) { union rt_ld a = { x }, b = { y }; a.b.se = (unsigned short) ((a.b.se & 0x7fff) | (b.b.se & 0x8000)); return a.d; }
long double nearbyintl(long double x) { long double r; __asm__ volatile("frndint" : "=t"(r) : "0"(x)); return r; }
long double rintl(long double x) { return nearbyintl(x); }
static long double rt_rndl(long double x, unsigned short rc) {
    unsigned short cw, nw; __asm__ volatile("fnstcw %0" : "=m"(cw)); nw = (unsigned short) ((cw & ~0xc00) | rc);
    long double r; __asm__ volatile("fldcw %2\n\tfrndint\n\tfldcw %3" : "=t"(r) : "0"(x), "m"(nw), "m"(cw)); return r;
}
long double truncl(long double x) { return rt_rndl(x, 0xc00); }
long double floorl(long double x) { return rt_rndl(x, 0x400); }
long double ceill(long double x) { return rt_rndl(x, 0x800); }
long double roundl(long double x) { long double r = truncl(x); if (fabsl(x - r) >= 0.5L) r += copysignl(1.0L, x); return r; }
long double ldexpl(long double x, int n) {
    if (n > 40000) n = 40000; else if (n < -40000) n = -40000;
    long double nl = (long double) n, r; __asm__("fscale" : "=t"(r) : "0"(x), "u"(nl)); return r;
}
long double scalbnl(long double x, int n) { return ldexpl(x, n); }
long double frexpl(long double x, int *e) {
    union rt_ld v = { x }; int ex = v.b.se & 0x7fff; *e = 0;
    if (ex == 0x7fff || (v.b.m == 0)) return x;
    if (ex == 0) { v.d = x * 18446744073709551616.0L; ex = v.b.se & 0x7fff; *e = -64; }
    *e += ex - 0x3ffe; v.b.se = (unsigned short) ((v.b.se & 0x8000) | 0x3ffe); return v.d;
}
long double fmodl(long double x, long double y) { long double r; __asm__("1: fprem\n\tfnstsw %%ax\n\tsahf\n\tjp 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
long double remainderl(long double x, long double y) { long double r; __asm__("1: fprem1\n\tfnstsw %%ax\n\tsahf\n\tjp 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
long double atan2l(long double y, long double x) { long double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
long double nextafterl(long double x, long double y) {
    if (x != x || y != y) return x + y;
    if (x == y) return y;
    union rt_ld v = { x }; unsigned e = v.b.se & 0x7fff, sg = v.b.se & 0x8000;
    if (x == 0) { v.b.m = 1; v.b.se = (unsigned short) (y < 0 ? 0x8000 : 0); return v.d; }
    int inc = (y > x) == (x > 0);                                  /* the magnitude grows */
    if (inc) { if (v.b.m == ~0ULL) { v.b.m = 1ULL << 63; e++; } else { int was = (int) (v.b.m >> 63); v.b.m++; if (e == 0 && !was && (v.b.m >> 63)) e = 1; } }
    else { if (v.b.m == 1ULL << 63) { if (e > 1) { e--; v.b.m = ~0ULL; } else { e = 0; v.b.m = (1ULL << 63) - 1; } } else v.b.m--; }
    v.b.se = (unsigned short) (e | sg); return v.d;
}
long double fmal(long double, long double, long double);
/* e^x - 1: reduce by n*ln2 (split in two so n*hi is exact), f2xm1 on the rest, scale back */
long double expm1l(long double x) {
    if (x != x) return x;
    if (x > 0xb.17217f7d1cf79acp+10L) return HUGE_VALL;               /* ln(LDBL_MAX) */
    if (x < -47.0L) return -1.0L;
    const long double hi = 0xB17217F7D1ULL * 0x1p-40L, lo = 0xCF79ABC9E3B39803ULL * 0x1p-104L, l2e = 1.442695040888963407359924681001892137L;
    const long double l2e_lo = -(1.0L - 0xBE87ED0BULL / 4294967296.0L) * 0x1p-63L;                   /* log2(e) - l2e */
    long double n = truncl(x * l2e);                                    /* toward zero: r and 2^n - 1 never cancel against each other */
    long double r = (x - n * hi) - n * lo, t = r * l2e, m;
    long double dt = fmal(r, l2e, -t) + r * l2e_lo;                     /* what rounding t to 64 bits lost */
    __asm__("f2xm1" : "=t"(m) : "0"(t));
    m += (m + 1.0L) * 0.6931471805599453094L * dt;                      /* 2^(t+dt) - 1 */
    if (n == 0) return m;
    int ni = (int) n;
    if (ni > 16000) return ldexpl(m + 1.0L, ni) - 1.0L;                  /* near overflow 2^n alone would be infinity */
    return ldexpl(m, ni) + (ldexpl(1.0L, ni) - 1.0L);
}

/* ---- fma: exact in the current rounding mode.  The product is 128 bits, the sum is done in 256 bits with a sticky bit. */
typedef struct { rt_u64 w[4]; } rt_u256;
static rt_u256 rt_shl256(rt_u256 a, int n) {
    rt_u256 r = { { 0, 0, 0, 0 } }; int ws = n / 64, bs = n % 64;
    for (int i = 3; i >= ws; i--) { r.w[i] = a.w[i - ws] << bs; if (bs && i - ws - 1 >= 0) r.w[i] |= a.w[i - ws - 1] >> (64 - bs); }
    return r;
}
static rt_u256 rt_shr256s(rt_u256 a, int n) {                          /* >> n, the lowest bit ORed with everything shifted out */
    rt_u256 r = { { 0, 0, 0, 0 } }; int lost = 0;
    if (n <= 0) return a;
    if (n >= 256) { r.w[0] = (a.w[0] | a.w[1] | a.w[2] | a.w[3]) != 0; return r; }
    int ws = n / 64, bs = n % 64;
    for (int i = 0; i < ws; i++) lost |= a.w[i] != 0;
    if (bs) lost |= (a.w[ws] & ((1ULL << bs) - 1)) != 0;
    for (int i = 0; i + ws < 4; i++) { r.w[i] = a.w[i + ws] >> bs; if (bs && i + ws + 1 < 4) r.w[i] |= a.w[i + ws + 1] << (64 - bs); }
    r.w[0] |= (rt_u64) lost; return r;
}
static rt_u256 rt_add256(rt_u256 a, rt_u256 b) { rt_u256 r; unsigned c = 0; for (int i = 0; i < 4; i++) { unsigned __int128 s = (unsigned __int128) a.w[i] + b.w[i] + c; r.w[i] = (rt_u64) s; c = (unsigned) (s >> 64); } return r; }
static rt_u256 rt_sub256(rt_u256 a, rt_u256 b) { rt_u256 r; unsigned br = 0; for (int i = 0; i < 4; i++) { unsigned __int128 s = (unsigned __int128) a.w[i] - b.w[i] - br; r.w[i] = (rt_u64) s; br = (unsigned) (s >> 64) & 1; } return r; }
static int rt_cmp256(rt_u256 a, rt_u256 b) { for (int i = 3; i >= 0; i--) if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1; return 0; }
static int rt_bitlen256(rt_u256 a) { for (int i = 3; i >= 0; i--) if (a.w[i]) return i * 64 + 64 - __builtin_clzll(a.w[i]); return 0; }
static int rt_bit256(rt_u256 a, int i) { return i < 0 || i >= 256 ? 0 : (int) ((a.w[i / 64] >> (i % 64)) & 1); }
static int rt_low256(rt_u256 a, int n) {                              /* any of the lowest n bits set */
    for (int i = 0; i < 4 && n > 0; i++, n -= 64) { rt_u64 m = n >= 64 ? ~0ULL : ((1ULL << n) - 1); if (a.w[i] & m) return 1; }
    return 0;
}
typedef struct { int cls, sign, e; rt_u64 m; } rt_fp;                  /* cls 0 finite (m == 0: zero), 1 inf, 2 nan; value = m * 2^e, m normalized */
static rt_fp rt_norm(int sign, rt_u64 m, int e) { rt_fp f = { 0, sign, e, m }; if (m) { int s = __builtin_clzll(m); f.m = m << s; f.e = e - s; } return f; }
static rt_fp rt_unpack_d(double x) { union { double d; rt_u64 u; } v = { x }; int s = (int) (v.u >> 63), ex = (int) ((v.u >> 52) & 0x7ff); rt_u64 fr = v.u & ((1ULL << 52) - 1);
    if (ex == 0x7ff) { rt_fp f = { fr ? 2 : 1, s, 0, 0 }; return f; }
    return ex ? rt_norm(s, fr | (1ULL << 52), ex - 1075) : rt_norm(s, fr, -1074); }
static rt_fp rt_unpack_f(float x) { union { float d; unsigned u; } v = { x }; int s = (int) (v.u >> 31), ex = (int) ((v.u >> 23) & 0xff); unsigned fr = v.u & 0x7fffff;
    if (ex == 0xff) { rt_fp f = { fr ? 2 : 1, s, 0, 0 }; return f; }
    return ex ? rt_norm(s, fr | 0x800000u, ex - 150) : rt_norm(s, fr, -149); }
static rt_fp rt_unpack_l(long double x) { union rt_ld v = { x }; int s = v.b.se >> 15, ex = v.b.se & 0x7fff;
    if (ex == 0x7fff) { rt_fp f = { (v.b.m << 1) ? 2 : 1, s, 0, 0 }; return f; }
    return rt_norm(s, v.b.m, (ex ? ex : 1) - 16446); }
typedef struct { int kind, sign, q; rt_u64 M; } rt_res;                /* kind 0 finite (M * 2^q), 1 inf, 2 nan, 3 zero */
static rt_res rt_fma_core(rt_fp a, rt_fp b, rt_fp c, int P, int emin, int emax, int mode) {
    rt_res o = { 0, 0, 0, 0 };
    int ps = a.sign ^ b.sign;
    if (a.cls == 2 || b.cls == 2 || c.cls == 2) { o.kind = 2; return o; }
    int az = a.cls == 0 && !a.m, bz = b.cls == 0 && !b.m;
    if (a.cls == 1 || b.cls == 1) {
        if (az || bz) { o.kind = 2; return o; }
        if (c.cls == 1 && c.sign != ps) { o.kind = 2; return o; }
        o.kind = 1; o.sign = ps; return o;
    }
    if (c.cls == 1) { o.kind = 1; o.sign = c.sign; return o; }
    int pz = az || bz, cz = !c.m;
    if (pz && cz) { o.kind = 3; o.sign = ps == c.sign ? ps : (mode == 1); return o; }
    /* operands as 256-bit integers with the top bit at 254 */
    rt_u256 X = { { 0, 0, 0, 0 } }, C = { { 0, 0, 0, 0 } }; int Ex = 0, Ec = 0;
    if (!pz) {
        unsigned __int128 pm = (unsigned __int128) a.m * b.m; int s = 0; if (!(pm >> 127)) { pm <<= 1; s = 1; }
        X.w[0] = 0; X.w[1] = (rt_u64) (pm & 1) << 63; X.w[2] = (rt_u64) (pm >> 1); X.w[3] = (rt_u64) (pm >> 65);       /* pm << 127 */
        Ex = a.e + b.e - s - 127;
    }
    if (!cz) { C.w[3] = c.m >> 1; C.w[2] = c.m << 63; Ec = c.e - 191; }        /* c.m << 191 = (c.m << 63) in words 2..3 shifted by 128 */
    rt_u256 L, S; int Le, Se, Ls, Ss;
    if (pz) { L = C; Le = Ec; Ls = c.sign; S = X; Se = Ec; Ss = ps; }
    else if (cz) { L = X; Le = Ex; Ls = ps; S = C; Se = Ex; Ss = c.sign; }
    else {
        int cmp = Ex != Ec ? (Ex > Ec ? 1 : -1) : rt_cmp256(X, C);
        if (cmp >= 0) { L = X; Le = Ex; Ls = ps; S = C; Se = Ec; Ss = c.sign; } else { L = C; Le = Ec; Ls = c.sign; S = X; Se = Ex; Ss = ps; }
    }
    rt_u256 R = L, Z = { { 0, 0, 0, 0 } }; (void) Z;
    if (!(pz || cz)) {
        S = rt_shr256s(S, Le - Se);
        R = (Ls == Ss) ? rt_add256(L, S) : rt_sub256(L, S);
    }
    int sign = Ls, E = Le;
    int bl = rt_bitlen256(R);
    if (!bl) { o.kind = 3; o.sign = mode == 1; return o; }
    int t = bl - 1, f = E + t, q = f - P + 1; if (q < emin - P + 1) q = emin - P + 1;
    int sh = q - E; rt_u64 M;
    if (sh <= 0) { rt_u256 r = rt_shl256(R, -sh); M = r.w[0]; }
    else {
        rt_u256 Q = rt_shr256s(R, 0); int half = rt_bit256(R, sh - 1), st = rt_low256(R, sh - 1 < 256 ? sh - 1 : 256);
        if (sh >= 256) { Q.w[0] = Q.w[1] = Q.w[2] = Q.w[3] = 0; if (sh > 256) half = 0; if (sh > 256) st = 1; }
        else { rt_u256 sr; int ws = sh / 64, bs = sh % 64; for (int i = 0; i < 4; i++) { sr.w[i] = i + ws < 4 ? R.w[i + ws] >> bs : 0; if (bs && i + ws + 1 < 4) sr.w[i] |= R.w[i + ws + 1] << (64 - bs); } Q = sr; }
        M = Q.w[0];
        int up = 0;
        switch (mode) { case 0: up = half && (st || (M & 1)); break; case 1: up = (half || st) && sign; break; case 2: up = (half || st) && !sign; break; default: up = 0; }
        M += (rt_u64) up;
        if (P < 64 && M == (1ULL << P)) { M >>= 1; q++; } else if (P == 64 && M == 0 && up) { M = 1ULL << 63; q++; }
    }
    if (!M) { o.kind = 3; o.sign = sign; return o; }
    int top = q + (64 - __builtin_clzll(M)) - 1;
    if (top > emax) {
        int inf = mode == 0 || (mode == 2 && !sign) || (mode == 1 && sign);
        if (inf) { o.kind = 1; o.sign = sign; return o; }
        o.kind = 0; o.sign = sign; o.M = P == 64 ? ~0ULL : ((1ULL << P) - 1); o.q = emax - P + 1; return o;
    }
    o.kind = 0; o.sign = sign; o.M = M; o.q = q; return o;
}
static double rt_pack_d(rt_res r) {
    union { double d; rt_u64 u; } v;
    if (r.kind == 2) { v.u = 0x7ff8000000000000ULL; return v.d; }
    if (r.kind == 1) { v.u = ((rt_u64) r.sign << 63) | (0x7ffULL << 52); return v.d; }
    if (r.kind == 3) { v.u = (rt_u64) r.sign << 63; return v.d; }
    if (r.M < (1ULL << 52)) v.u = ((rt_u64) r.sign << 63) | r.M;
    else v.u = ((rt_u64) r.sign << 63) | ((rt_u64) (r.q + 52 + 1023) << 52) | (r.M & ((1ULL << 52) - 1));
    return v.d;
}
static float rt_pack_f(rt_res r) {
    union { float d; unsigned u; } v;
    if (r.kind == 2) { v.u = 0x7fc00000u; return v.d; }
    if (r.kind == 1) { v.u = ((unsigned) r.sign << 31) | 0x7f800000u; return v.d; }
    if (r.kind == 3) { v.u = (unsigned) r.sign << 31; return v.d; }
    if (r.M < (1u << 23)) v.u = ((unsigned) r.sign << 31) | (unsigned) r.M;
    else v.u = ((unsigned) r.sign << 31) | ((unsigned) (r.q + 23 + 127) << 23) | ((unsigned) r.M & 0x7fffff);
    return v.d;
}
static long double rt_pack_l(rt_res r) {
    union rt_ld v; v.b.m = 0;
    if (r.kind == 2) { v.b.m = 0xc000000000000000ULL; v.b.se = 0x7fff; return v.d; }
    if (r.kind == 1) { v.b.m = 1ULL << 63; v.b.se = (unsigned short) (0x7fff | (r.sign << 15)); return v.d; }
    if (r.kind == 3) { v.b.m = 0; v.b.se = (unsigned short) (r.sign << 15); return v.d; }
    v.b.m = r.M;
    v.b.se = (unsigned short) ((r.M >> 63 ? r.q + 63 + 16383 : 0) | (r.sign << 15));
    return v.d;
}
static int rt_hw_fma = -1;
static int rt_have_fma(void) {
    if (rt_hw_fma < 0) {
        unsigned a = 1, b, c, d; __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
        int ok = (c >> 12 & 1) && (c >> 27 & 1) && (c >> 28 & 1);                       /* FMA, OSXSAVE, AVX */
        if (ok) { unsigned xl, xh; __asm__ volatile("xgetbv" : "=a"(xl), "=d"(xh) : "c"(0)); ok = (xl & 6) == 6; }
        rt_hw_fma = ok;
    }
    return rt_hw_fma;
}
static int rt_force_soft_fma;                                                              /* tests */
__attribute__((target("fma"))) static double rt_fma_hw(double a, double b, double c) { return __builtin_fma(a, b, c); }
__attribute__((target("fma"))) static float rt_fmaf_hw(float a, float b, float c) { return __builtin_fmaf(a, b, c); }
double fma(double a, double b, double c) {
    if (!rt_force_soft_fma && rt_have_fma()) return rt_fma_hw(a, b, c);
    return rt_pack_d(rt_fma_core(rt_unpack_d(a), rt_unpack_d(b), rt_unpack_d(c), 53, -1022, 1023, rt_mxmode()));
}
float fmaf(float a, float b, float c) {
    if (!rt_force_soft_fma && rt_have_fma()) return rt_fmaf_hw(a, b, c);
    return rt_pack_f(rt_fma_core(rt_unpack_f(a), rt_unpack_f(b), rt_unpack_f(c), 24, -126, 127, rt_mxmode()));
}
long double fmal(long double a, long double b, long double c) {
    return rt_pack_l(rt_fma_core(rt_unpack_l(a), rt_unpack_l(b), rt_unpack_l(c), 64, -16382, 16383, rt_mxmode()));
}
#endif /* x86_64 / aarch64 */

#endif /* PEMU_RT_MATH_H */

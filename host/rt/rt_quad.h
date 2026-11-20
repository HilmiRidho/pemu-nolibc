/* host/rt/rt_quad.h - IEEE binary128 math on integers, for the libc-free build of an aarch64 host (where long double is binary128).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure C, no libm and no host asm: it only needs the four basic operations of the type (libgcc's __addtf3 / __multf3 / __divtf3 ...,
 * which the compiler calls itself) and unsigned __int128.  Because nothing here is aarch64 specific, tests/integration/rt_quad.sh builds and checks
 * the very same code on an x86_64 machine, where the type is __float128 (against libquadmath and an exact Python oracle).
 *
 * Every function takes the rounding mode as a parameter (0 nearest-even, 1 toward -inf, 2 toward +inf, 3 toward zero), so the caller
 * decides where it comes from: rt_math.h reads FPCR on aarch64, the tests pass it in.  sqrt, fma, ldexp, fmod, remainder, the integer
 * roundings, frexp and nextafter are exact (correctly rounded in the given mode); expm1 and atan2 are within a few ulp. */
#ifndef PEMU_RT_QUAD_H
#define PEMU_RT_QUAD_H
#include <float.h>
typedef unsigned long long rt_u64;
typedef unsigned __int128 rt_u128;
#if LDBL_MANT_DIG == 113
typedef long double rt_q;
#else
typedef __float128 rt_q;
#endif

#define RTQ_MASK112 ((((rt_u128) 1) << 112) - 1)
#define RTQ_SIGN    (((rt_u128) 1) << 127)
static inline rt_u128 rt_q_bits(rt_q x) { union { rt_q f; rt_u128 b; } u; u.f = x; return u.b; }
static inline rt_q rt_q_from(rt_u128 b) { union { rt_q f; rt_u128 b; } u; u.b = b; return u.f; }
#define RTQ_C(hi, lo) rt_q_from((((rt_u128) (hi)) << 64) | (lo))
static inline rt_q rt_q_inf(int sign) { return rt_q_from(((rt_u128) sign << 127) | ((rt_u128) 0x7fff << 112)); }
static inline rt_q rt_q_nan(void) { return rt_q_from((rt_u128) 0x7fff8000ULL << 96); }
static inline rt_q rt_q_quiet(rt_q x) { return rt_q_from(rt_q_bits(x) | ((rt_u128) 1 << 111)); }
static inline int rt_q_isnan(rt_q x) { rt_u128 b = rt_q_bits(x); return ((b >> 112) & 0x7fff) == 0x7fff && (b & RTQ_MASK112); }
static inline rt_q rt_q_fabs(rt_q x) { return rt_q_from(rt_q_bits(x) & ~RTQ_SIGN); }
static inline rt_q rt_q_copysign(rt_q x, rt_q y) { return rt_q_from((rt_q_bits(x) & ~RTQ_SIGN) | (rt_q_bits(y) & RTQ_SIGN)); }
static inline int rt_q_bl(rt_u128 m) { rt_u64 h = (rt_u64) (m >> 64); if (h) return 128 - __builtin_clzll(h); rt_u64 l = (rt_u64) m; return l ? 64 - __builtin_clzll(l) : 0; }

/* finite: value = m * 2^q (m < 2^113, not normalized); cls 0 finite (m == 0: zero), 1 infinity, 2 NaN */
typedef struct { int cls, sign, q; rt_u128 m; } rt_qv;
static rt_qv rt_q_unpack(rt_q x) {
    rt_u128 b = rt_q_bits(x); rt_qv v; int ex = (int) ((b >> 112) & 0x7fff); rt_u128 fr = b & RTQ_MASK112;
    v.sign = (int) (b >> 127);
    if (ex == 0x7fff) { v.cls = fr ? 2 : 1; v.m = fr; v.q = 0; return v; }
    v.cls = 0; v.m = ex ? (fr | ((rt_u128) 1 << 112)) : fr; v.q = (ex ? ex : 1) - 16495; return v;
}
/* M >> sh with the bit below (half) and "anything below that" (rest) */
static void rt_q_shr(rt_u128 M, int sh, rt_u128 *Q, int *half, int *rest) {
    if (sh <= 0) { *Q = M << -sh; *half = *rest = 0; }
    else if (sh >= 129) { *Q = 0; *half = 0; *rest = M != 0; }
    else if (sh == 128) { *Q = 0; *half = (int) (M >> 127); *rest = (M & ~RTQ_SIGN) != 0; }
    else { *Q = M >> sh; *half = (int) ((M >> (sh - 1)) & 1); *rest = (M & ((((rt_u128) 1) << (sh - 1)) - 1)) != 0; }
}
static int rt_q_up(int mode, int sign, int odd, int half, int rest) {   /* does the magnitude grow by one unit? */
    if (!(half | rest)) return 0;
    switch (mode) { case 0: return half && (rest || odd); case 1: return sign; case 2: return !sign; case 4: return half; default: return 0; }
}
/* the correctly rounded value of (-1)^sign * (M + eps) * 2^q, where eps is "something below the last bit" if sticky.  M has at most 126 bits;
 * with sticky it needs at least 114 (so the sticky bit sits below the rounding bit).  Handles subnormals and overflow. */
static rt_q rt_q_pack(int sign, rt_u128 M, int q, int sticky, int mode) {
    rt_u128 SB = (rt_u128) sign << 127;
    if (!M) return rt_q_from(SB);
    if (sticky) { M = (M << 1) | 1; q--; }
    int bl = rt_q_bl(M), f = q + bl - 1, qt = f - 112;
    if (qt < -16494) qt = -16494;
    rt_u128 Q; int half, rest;
    rt_q_shr(M, qt - q, &Q, &half, &rest);
    Q += (rt_u128) rt_q_up(mode, sign, (int) (Q & 1), half, rest);
    if (Q >> 113) { Q >>= 1; qt++; }
    if (!Q) return rt_q_from(SB);
    int ex = (Q >> 112) ? qt + 16495 : 0;
    if (ex >= 0x7fff) {
        if (mode == 0 || (mode == 1 && sign) || (mode == 2 && !sign)) return rt_q_inf(sign);
        return rt_q_from(SB | ((rt_u128) 0x7ffe << 112) | RTQ_MASK112);
    }
    return rt_q_from(SB | ((rt_u128) ex << 112) | (Q & RTQ_MASK112));
}

/* ---- sqrt: integer square root of m * 2^118 (digit by digit), the remainder gives the sticky bit */
static rt_q rt_q_sqrt(rt_q x, int mode) {
    rt_qv v = rt_q_unpack(x);
    if (v.cls == 2) return rt_q_quiet(x);
    if (v.cls == 1) return v.sign ? rt_q_nan() : x;
    if (!v.m) return x;
    if (v.sign) return rt_q_nan();
    rt_u128 M = v.m; int q = v.q, s = 114 - rt_q_bl(M);
    M <<= s; q -= s;
    if (q & 1) { M <<= 1; q--; }                                       /* q even; M has 114 or 115 bits */
    rt_u128 res = 0, rem = 0;
    for (int p = 116; p >= 0; p--) {
        int k = 2 * p - 118; unsigned pr = 0;                          /* the two bits of M * 2^118 at positions 2p+1, 2p */
        if (k + 1 >= 0) pr |= (unsigned) ((M >> (k + 1)) & 1) << 1;
        if (k >= 0) pr |= (unsigned) ((M >> k) & 1);
        rem = (rem << 2) | pr;
        rt_u128 t = (res << 2) | 1;
        if (rem >= t) { rem -= t; res = (res << 1) | 1; } else res <<= 1;
    }
    return rt_q_pack(0, res, (q - 118) / 2, rem != 0, mode);
}

/* ---- rounding to an integer: kind 0 nearest-even (rint), 1 floor, 2 ceil, 3 trunc, 4 half away from zero (round) */
static rt_q rt_q_round(rt_q x, int kind) {
    rt_qv v = rt_q_unpack(x);
    if (v.cls) return v.cls == 2 ? rt_q_quiet(x) : x;
    if (!v.m || v.q >= 0) return x;
    rt_u128 Q; int half, rest;
    rt_q_shr(v.m, -v.q, &Q, &half, &rest);
    int mode = kind == 1 ? 1 : kind == 2 ? 2 : kind == 3 ? 3 : kind == 4 ? 4 : 0;
    Q += (rt_u128) rt_q_up(mode, v.sign, (int) (Q & 1), half, rest);
    return rt_q_pack(v.sign, Q, 0, 0, 0);                                 /* exact; Q == 0 gives the signed zero */
}

/* ---- ldexp / frexp */
static rt_q rt_q_ldexp(rt_q x, int n, int mode) {
    rt_qv v = rt_q_unpack(x);
    if (v.cls || !v.m) return v.cls == 2 ? rt_q_quiet(x) : x;
    if (n > 40000) n = 40000; else if (n < -40000) n = -40000;
    return rt_q_pack(v.sign, v.m, v.q + n, 0, mode);
}
static rt_q rt_q_frexp(rt_q x, int *e) {
    rt_qv v = rt_q_unpack(x); *e = 0;
    if (v.cls || !v.m) return v.cls == 2 ? rt_q_quiet(x) : x;
    int bl = rt_q_bl(v.m); *e = v.q + bl;
    return rt_q_pack(v.sign, v.m, -bl, 0, 0);
}

/* ---- fmod (rem == 0) and remainder (rem == 1): exact, by long division in 14-bit steps */
static rt_q rt_q_fmod(rt_q x, rt_q y, int rem) {
    rt_qv a = rt_q_unpack(x), b = rt_q_unpack(y);
    if (a.cls == 2) return rt_q_quiet(x);
    if (b.cls == 2) return rt_q_quiet(y);
    if (a.cls == 1 || (b.cls == 0 && !b.m)) return rt_q_nan();
    if (b.cls == 1 || !a.m) return x;
    rt_u128 Mx = a.m, My = b.m, r, ql = 0; int qr, sign = a.sign;
    if (a.q >= b.q) {
        int d = a.q - b.q; qr = b.q;
        r = Mx % My; ql = Mx / My;
        while (d > 0) { int k = d > 14 ? 14 : d; rt_u128 t = r << k; ql = t / My; r = t - ql * My; d -= k; }
    } else {
        int e = b.q - a.q;                                              /* then |x| < |y| */
        if (e >= 3) return x;                                           /* |x| < |y| / 2 */
        My <<= e; r = Mx; qr = a.q;
    }
    if (rem) {
        rt_u128 r2 = r << 1;
        if (r2 > My || (r2 == My && (ql & 1))) { r = My - r; sign ^= 1; }
    }
    return rt_q_pack(sign, r, qr, 0, 0);
}

static rt_q rt_q_nextafter(rt_q x, rt_q y) {
    if (rt_q_isnan(x)) return rt_q_quiet(x);
    if (rt_q_isnan(y)) return rt_q_quiet(y);
    rt_u128 bx = rt_q_bits(x), by = rt_q_bits(y);
    if (bx == by || !((bx | by) & ~RTQ_SIGN)) return y;                  /* equal, or both zero */
    if (!(bx & ~RTQ_SIGN)) return rt_q_from((by & RTQ_SIGN) | 1);        /* x is zero: the smallest subnormal toward y */
    int inc = (y > x) == !(bx & RTQ_SIGN);                               /* the magnitude grows */
    rt_u128 mag = (bx & ~RTQ_SIGN) + (inc ? 1 : (rt_u128) -1);
    return rt_q_from((bx & RTQ_SIGN) | mag);
}

/* ---- expm1: x = n ln2 + r, expm1(r) by a Taylor series on r / 16 (Horner) and four doublings, then 2^n (1 + s) - 1.  A few ulp. */
static rt_q rt_q_expm1(rt_q x, int mode) {
    if (rt_q_isnan(x)) return rt_q_quiet(x);
    if (x > (rt_q) 11356.5234062941439L) return (mode == 0 || mode == 2) ? rt_q_inf(0) : rt_q_from(((rt_u128) 0x7ffe << 112) | RTQ_MASK112);
    if (x < (rt_q) -80.0L) return -1;
    rt_q ax = rt_q_fabs(x);
    if (ax < (rt_q) 0x1p-60L) return x + x * x * (rt_q) 0.5L;
    const rt_q LOG2E = RTQ_C(0x3fff71547652b82fULL, 0xe1777d0ffda0d23aULL);
    const rt_q LN2_HI = RTQ_C(0x3ffe62e42fefa39eULL, 0xf35793c767300000ULL);     /* 96 bits: n * LN2_HI is exact for |n| < 2^17 */
    const rt_q LN2_LO = RTQ_C(0x3f98f97b57a079a1ULL, 0x93394c5b16c5068cULL);
    rt_q n = rt_q_round(x * LOG2E, 0);
    int ni = (int) n;
    rt_q r = (x - n * LN2_HI) - n * LN2_LO, a = r * (rt_q) 0.0625L, p = 1;
    for (int k = 18; k >= 2; k--) p = 1 + a * p / (rt_q) k;
    rt_q s = a * p;
    for (int i = 0; i < 4; i++) s = 2 * s + s * s;
    if (ni == 0) return s;
    if (ni >= 114) return rt_q_ldexp(1 + s, ni, 0);              /* the "- 1" is below half an ulp */
    return rt_q_ldexp(s, ni, 0) + (rt_q_ldexp(1, ni, 0) - 1);
}

/* ---- atan2: atan of the ratio in [0, 1] by three half-angle steps t -> t / (1 + sqrt(1 + t^2)) and a series, then the quadrant. A few ulp. */
static rt_q rt_q_atan01(rt_q t) {
    for (int i = 0; i < 3; i++) t = t / (1 + rt_q_sqrt(1 + t * t, 0));
    rt_q u2 = t * t, p = 0;
    for (int k = 20; k >= 0; k--) p = (rt_q) 1 / (rt_q) (2 * k + 1) - u2 * p;
    return 8 * (t * p);
}
static rt_q rt_q_atan2(rt_q y, rt_q x) {
    if (rt_q_isnan(x)) return rt_q_quiet(x);
    if (rt_q_isnan(y)) return rt_q_quiet(y);
    const rt_q PIO2_HI = RTQ_C(0x3fff921fb54442d1ULL, 0x8469898cc51701b8ULL), PIO2_LO = RTQ_C(0x3f8ccd129024e088ULL, 0xa67cc74020bbea64ULL);
    int sy = (int) (rt_q_bits(y) >> 127), sx = (int) (rt_q_bits(x) >> 127);
    rt_q ay = rt_q_fabs(y), ax = rt_q_fabs(x), base;
    int yz = rt_q_bits(ay) == 0, xz = rt_q_bits(ax) == 0, yi = rt_q_bits(ay) == rt_q_bits(rt_q_inf(0)), xi = rt_q_bits(ax) == rt_q_bits(rt_q_inf(0));
    if (yi && xi) base = sx ? 3 * (PIO2_HI * (rt_q) 0.5L) + 3 * (PIO2_LO * (rt_q) 0.5L) : PIO2_HI * (rt_q) 0.5L + PIO2_LO * (rt_q) 0.5L;   /* 3pi/4 or pi/4 */
    else if (yz) base = sx ? 2 * PIO2_HI + 2 * PIO2_LO : (rt_q) 0;       /* (2 * PIO2_HI) + (2 * PIO2_LO): rounded to nearest, a quad pi */
    else if (xz || yi) base = PIO2_HI + PIO2_LO;
    else if (xi) base = sx ? 2 * PIO2_HI + 2 * PIO2_LO : (rt_q) 0;
    else {
        if (ay <= ax) base = rt_q_atan01(ay / ax);
        else base = (PIO2_HI - rt_q_atan01(ax / ay)) + PIO2_LO;
        if (sx) base = (2 * PIO2_HI - base) + 2 * PIO2_LO;
    }
    return sy ? -base : base;
}

/* ---- fma: product (226 bits) and addend aligned in a 512-bit integer with the top bit at 510, the smaller one shifted right with a sticky bit
 * (it has at least 285 zero bits below its top 226, so nothing but the sticky is ever lost), then one rounding.  Exact in every mode. */
#define RTQ_NL 8
typedef struct { rt_u64 w[RTQ_NL]; } rt_u512;
static rt_u512 rt_q_shl512(rt_u512 a, int n) {
    rt_u512 r; int ws = n / 64, bs = n % 64;
    for (int i = RTQ_NL - 1; i >= 0; i--) {
        rt_u64 v = i - ws >= 0 ? a.w[i - ws] << bs : 0;
        if (bs && i - ws - 1 >= 0) v |= a.w[i - ws - 1] >> (64 - bs);
        r.w[i] = v;
    }
    return r;
}
static rt_u512 rt_q_shr512s(rt_u512 a, int n) {                          /* >> n, the lowest bit ORed with everything shifted out */
    rt_u512 r; int lost = 0, ws = n / 64, bs = n % 64;
    for (int i = 0; i < RTQ_NL; i++) r.w[i] = 0;
    if (n <= 0) return a;
    if (n >= 64 * RTQ_NL) { for (int i = 0; i < RTQ_NL; i++) lost |= a.w[i] != 0; r.w[0] = (rt_u64) lost; return r; }
    for (int i = 0; i < ws; i++) lost |= a.w[i] != 0;
    if (bs) lost |= (a.w[ws] & ((1ULL << bs) - 1)) != 0;
    for (int i = 0; i + ws < RTQ_NL; i++) {
        r.w[i] = a.w[i + ws] >> bs;
        if (bs && i + ws + 1 < RTQ_NL) r.w[i] |= a.w[i + ws + 1] << (64 - bs);
    }
    r.w[0] |= (rt_u64) lost; return r;
}
static rt_u512 rt_q_add512(rt_u512 a, rt_u512 b) { rt_u512 r; unsigned c = 0; for (int i = 0; i < RTQ_NL; i++) { rt_u128 s = (rt_u128) a.w[i] + b.w[i] + c; r.w[i] = (rt_u64) s; c = (unsigned) (s >> 64); } return r; }
static rt_u512 rt_q_sub512(rt_u512 a, rt_u512 b) { rt_u512 r; unsigned br = 0; for (int i = 0; i < RTQ_NL; i++) { rt_u128 s = (rt_u128) a.w[i] - b.w[i] - br; r.w[i] = (rt_u64) s; br = (unsigned) (s >> 64) & 1; } return r; }
static int rt_q_cmp512(rt_u512 a, rt_u512 b) { for (int i = RTQ_NL - 1; i >= 0; i--) if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1; return 0; }
static int rt_q_bl512(rt_u512 a) { for (int i = RTQ_NL - 1; i >= 0; i--) if (a.w[i]) return i * 64 + 64 - __builtin_clzll(a.w[i]); return 0; }
static rt_u512 rt_q_mul113(rt_u128 a, rt_u128 b) {                       /* a, b < 2^113 */
    rt_u64 a0 = (rt_u64) a, a1 = (rt_u64) (a >> 64), b0 = (rt_u64) b, b1 = (rt_u64) (b >> 64);
    rt_u128 p00 = (rt_u128) a0 * b0, mid = (rt_u128) a0 * b1 + (rt_u128) a1 * b0, p11 = (rt_u128) a1 * b1;
    rt_u512 r; for (int i = 0; i < RTQ_NL; i++) r.w[i] = 0;
    r.w[0] = (rt_u64) p00; r.w[1] = (rt_u64) (p00 >> 64);
    rt_u128 s = (rt_u128) r.w[1] + (rt_u64) mid; r.w[1] = (rt_u64) s;
    rt_u128 t = (mid >> 64) + (s >> 64) + (rt_u64) p11; r.w[2] = (rt_u64) t;
    t = (t >> 64) + (p11 >> 64); r.w[3] = (rt_u64) t;
    return r;
}
static rt_u512 rt_q_u128_to512(rt_u128 a) { rt_u512 r; for (int i = 0; i < RTQ_NL; i++) r.w[i] = 0; r.w[0] = (rt_u64) a; r.w[1] = (rt_u64) (a >> 64); return r; }
static rt_q rt_q_fma(rt_q xa, rt_q xb, rt_q xc, int mode) {
    rt_qv a = rt_q_unpack(xa), b = rt_q_unpack(xb), c = rt_q_unpack(xc);
    int ps = a.sign ^ b.sign;
    if (a.cls == 2 || b.cls == 2 || c.cls == 2) return rt_q_nan();
    int az = a.cls == 0 && !a.m, bz = b.cls == 0 && !b.m;
    if (a.cls == 1 || b.cls == 1) {
        if (az || bz) return rt_q_nan();
        if (c.cls == 1 && c.sign != ps) return rt_q_nan();
        return rt_q_inf(ps);
    }
    if (c.cls == 1) return xc;
    int pz = az || bz, cz = !c.m;
    if (pz && cz) return rt_q_from((rt_u128) (ps == c.sign ? ps : (mode == 1)) << 127);
    rt_u512 X, C, L, S, R; int Ex = 0, Ec = 0, Le, Se, Ls, Ss, sign, E;
    if (!pz) {
        X = rt_q_mul113(a.m, b.m);
        int s = 510 - (rt_q_bl512(X) - 1); X = rt_q_shl512(X, s); Ex = a.q + b.q - s;
    }
    if (!cz) {
        C = rt_q_u128_to512(c.m);
        int s = 510 - (rt_q_bl512(C) - 1); C = rt_q_shl512(C, s); Ec = c.q - s;
    }
    if (pz) { R = C; E = Ec; sign = c.sign; }
    else if (cz) { R = X; E = Ex; sign = ps; }
    else {
        int cmp = Ex != Ec ? (Ex > Ec ? 1 : -1) : rt_q_cmp512(X, C);
        if (cmp >= 0) { L = X; Le = Ex; Ls = ps; S = C; Se = Ec; Ss = c.sign; } else { L = C; Le = Ec; Ls = c.sign; S = X; Se = Ex; Ss = ps; }
        S = rt_q_shr512s(S, Le - Se);
        R = Ls == Ss ? rt_q_add512(L, S) : rt_q_sub512(L, S);
        sign = Ls; E = Le;
    }
    int bl = rt_q_bl512(R);
    if (!bl) return rt_q_from((rt_u128) (mode == 1) << 127);             /* exact cancellation: +0, or -0 rounding down */
    if (bl > 126) { R = rt_q_shr512s(R, bl - 126); E += bl - 126; }       /* the lowest bit is now the sticky of everything below */
    rt_u128 M = (rt_u128) R.w[0] | ((rt_u128) R.w[1] << 64);
    return rt_q_pack(sign, M, E, 0, mode);
}
#endif /* PEMU_RT_QUAD_H */

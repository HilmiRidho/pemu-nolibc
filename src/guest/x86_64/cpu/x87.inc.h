/* src/guest/x86_64/cpu/x87.inc.h - x87 subset. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ instruction step */
/* ------------------------------------------------------------ x87 (subset) */
/* Values are held as host long double: exact 80-bit on x86_64 hosts; IEEE quad
 * (slightly more precise) on aarch64 hosts. Memory format is always 80-bit. */
#define ST(i) (c->st[(c->top + (i)) & 7])
#include <float.h>
#include <fenv.h>
#define X87_NATIVE (LDBL_MANT_DIG == 64)
#ifdef PEMU_X87_FORCE_RB                                /* test knob: run the extra rounding stage meant for wider hosts on an x86_64 host too (it must change nothing) */
#define X87_WIDE 1
#else
#define X87_WIDE (!X87_NATIVE)
#endif                /* host long double is the x87 80-bit format (x86_64 host) */
/* NaN payloads: an 80-bit NaN is kept bit for bit (sign, quiet bit, payload), so that a signalling NaN can be told from a quiet one and
 * a load/store pair does not change it. x86_64 host: the 10 bytes are the long double. IEEE quad host (aarch64): fraction bit 111 is the quiet
 * bit, the 63 fraction bits of the 80-bit format sit at the top of the 112-bit fraction. */
static long double ld_from80(const u8*p) {
    u64 m = *(u64u*) p; u16 se = *(u16u*)(p + 8); int e = se & 0x7fff; long double v;
#if X87_NATIVE
    if (e == 0x7fff && (m << 1)) { long double t = 0; memcpy(&t, p, 10); return t; }                 /* NaN, bit exact */
#elif LDBL_MANT_DIG == 113
    if (e == 0x7fff && (m << 1)) { u64 q[2]; q[0] = m << 49; q[1] = ((u64) se << 48) | ((m & 0x7fffffffffffffffULL) >> 15); long double t; memcpy(&t, q, 16); return t; }
#endif
    if (e == 0x7fff) v = (m << 1) ? (long double) NAN : (long double) INFINITY;
    else if (!m) v = 0;
    else v = ldexpl((long double) m, (e ? e : 1) - 16383 - 63);
    return (se >> 15) ? -v : v;
}
static void ld_to80(long double v, u8*p) {
    u64 m = 0; u16 se = 0;
    if (isnan(v)) {
#if X87_NATIVE
        memcpy(p, &v, 10); return;
#elif LDBL_MANT_DIG == 113
        u64 q[2]; memcpy(q, &v, 16);
        *(u64u*) p = 0x8000000000000000ULL | ((q[1] & 0x0000ffffffffffffULL) << 15) | (q[0] >> 49); *(u16u*)(p + 8) = (u16)(q[1] >> 48); return;
#else
        *(u64u*) p = 0xC000000000000000ULL; *(u16u*)(p + 8) = (u16)(signbit(v) ? 0xffff : 0x7fff); return;
#endif
    }
    if (signbit(v)) { se = 0x8000; v = -v; }
    if (isinf(v)) { se |= 0x7fff; m = 0x8000000000000000ULL; }
    else if (v != 0) {
        int e; long double f = frexpl(v, &e);
        long double sc = nearbyintl(ldexpl(f, 64));
        if (sc >= 18446744073709551616.0L) { m = 0x8000000000000000ULL; e++; } else m = (u64) sc;
        int be = e - 1 + 16383;
        if (be <= 0) {                                                  /* denormal: biased exponent 0, value = m * 2^(1-16383-63); m = 2^63 means it rounded up to the smallest normal */
            long double dm = nearbyintl(ldexpl(v, 16382 + 63)); m = (u64) dm; be = (m >> 63) ? 1 : 0;
        } else if (be >= 0x7fff) { be = 0x7fff; m = 0x8000000000000000ULL; }
        se |= (u16) be;
    }
    *(u64u*) p = m; *(u16u*)(p + 8) = se;
}
/* ---- exception flags. fsw keeps IE DE ZE OE UE PE SF and C0-C3; TOP is `top`; ES and B are derived (an exception flagged whose mask bit in fcw is clear).
 * An unmasked exception is raised (SIGFPE) by the next waiting x87 instruction or fwait, as on real hardware. An unmasked IE / ZE / DE stops the arithmetic
 * instruction before it stores anything (x_begin / x_end); an unmasked IE / OE / UE stops fst m32 / m64 and IE stops fist / fisttp (x_endm). An unmasked OE / UE
 * completes with the rounded result whose exponent is brought back by -24576 / +24576 (x_fast, the tiny path of x_arith, and the overflow branch at the end of x_arith
 * for an addition with a denormal operand). */
enum { X_IE = 1, X_DE = 2, X_ZE = 4, X_OE = 8, X_UE = 16, X_PE = 32, X_SF = 64, X_C1 = 0x200 };
static void x_exc(CPU*c, int b) { c->fsw |= (u16) b; if (b & X_SF) c->fsw |= X_IE; }
static int x_pend(CPU*c) { return (c->fsw & 0x3f & ~c->fcw) != 0; }
static u16 x_sw(CPU*c) { u16 sw = (u16)((c->fsw & 0x477f) | (c->top << 11)); if (x_pend(c)) sw |= 0x8080; return sw; }
static int x_trap(CPU*c) { g_os->exception(c, EXCP_MF); return 1; }
static int x_issnan(long double v) {
#if X87_NATIVE
    if (!isnan(v)) return 0;
    u64 m; memcpy(&m, &v, 8); return !(m & 0x4000000000000000ULL);
#elif LDBL_MANT_DIG == 113
    if (!isnan(v)) return 0; u64 q[2]; memcpy(q, &v, 16); return !(q[1] & 0x0000800000000000ULL);
#else
    (void) v; return 0;
#endif
}
static long double x_indef(void) { static const u8 b[10] = { 0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0xFF }; return ld_from80(b); }       /* real indefinite: negative QNaN */
static long double x_quiet(long double v) { u8 b[10]; ld_to80(v, b); b[7] |= 0x40; return ld_from80(b); }
static long double x_nanpick(long double a, long double b) {     /* result of an operation with a NaN operand: the quieted NaN; two NaNs: the quiet one, else the larger significand */
    if (!isnan(a)) return x_quiet(b);
    if (!isnan(b)) return x_quiet(a);
    int sa = x_issnan(a), sb = x_issnan(b);
    if (sa != sb) return sa ? x_quiet(b) : x_quiet(a);
    u8 ba[10], bb[10]; ld_to80(x_quiet(a), ba); ld_to80(x_quiet(b), bb);
    u64 ma = *(u64u*) ba, mb = *(u64u*) bb;
    if (ma != mb) return ma > mb ? x_quiet(a) : x_quiet(b);
    return signbit(a) ? x_quiet(b) : x_quiet(a);
}
static int x_den(long double v) { return fpclassify(v) == FP_SUBNORMAL; }
static long double x_from_f(CPU*c, float f) {                    /* fld m32 */
    if (isnan(f)) { u32 b; memcpy(&b, &f, 4); u8 o[10]; if (!(b & 0x400000)) x_exc(c, X_IE);
        *(u64u*) o = 0xC000000000000000ULL | ((u64)(b & 0x3fffff) << 40); *(u16u*)(o + 8) = (u16)((b >> 31) ? 0xffff : 0x7fff); return ld_from80(o); }
    if (fpclassify(f) == FP_SUBNORMAL) x_exc(c, X_DE);
    return f;
}
static long double x_from_d(CPU*c, double d) {                   /* fld m64 */
    if (isnan(d)) { u64 b; memcpy(&b, &d, 8); u8 o[10]; if (!(b & 0x8000000000000ULL)) x_exc(c, X_IE);
        *(u64u*) o = 0xC000000000000000ULL | ((b & 0x3ffffffffffffULL) << 11); *(u16u*)(o + 8) = (u16)((b >> 63) ? 0xffff : 0x7fff); return ld_from80(o); }
    if (fpclassify(d) == FP_SUBNORMAL) x_exc(c, X_DE);
    return d;
}
/* FXSAVE image (512 bytes), shared by fxsave/fxrstor, the signal frame and sigreturn. Layout: 0 fcw, 2 fsw (TOP in bits 11-13), 4 abridged tag word (bit n = physical
 * register n not empty), 24 mxcsr, 28 mxcsr_mask, 32 ST(0..7) at 16 bytes each in stack order, 160 xmm0-15. */
static void fx_save(CPU*c, u8*p) {
    memset(p, 0, 512);
    *(u16u*) p = c->fcw; *(u16u*)(p + 2) = x_sw(c); p[4] = c->ftw;
    *(u32u*)(p + 24) = c->mxcsr; *(u32u*)(p + 28) = 0xffff;
    for (int i = 0; i < 8; i++) ld_to80(c->st[(c->top + i) & 7], p + 32 + 16 * i);
    memcpy(p + 160, c->x, 256);
}
static void fx_restore(CPU*c, const u8*p) {
    u16 sw = *(const u16u*)(p + 2);
    c->fcw = *(const u16u*) p; c->fsw = sw & 0x477f; c->top = (sw >> 11) & 7;
    c->ftw = p[4]; c->mxcsr = *(const u32u*)(p + 24);
    for (int i = 0; i < 8; i++) c->st[(c->top + i) & 7] = ld_from80(p + 32 + 16 * i);
    memcpy(c->x, p + 160, 256);
}
static void xpush(CPU*c, long double v) {
    int nt = (c->top - 1) & 7;
    if ((c->ftw >> nt) & 1) { x_exc(c, X_SF); c->fsw |= X_C1; v = x_indef(); }          /* stack overflow: the register is not empty */
    c->top = nt; c->st[nt] = v; c->ftw |= (u8)(1u << nt);
}
static void xpop(CPU*c) {
    if (!((c->ftw >> c->top) & 1)) { x_exc(c, X_SF); c->fsw &= ~X_C1; }                 /* stack underflow */
    c->ftw &= (u8) ~(1u << c->top); c->top = (c->top + 1) & 7;
}
/* read ST(i): an empty register is a stack underflow (IE + SF, C1 = 0) and reads as the real indefinite */
static long double x_rd(CPU*c, int i) {
    int p = (c->top + i) & 7;
    if (__builtin_expect(!((c->ftw >> p) & 1), 0)) { x_exc(c, X_SF); c->fsw &= ~X_C1; return x_indef(); }
    return c->st[p];
}
#define SR(i) x_rd(c, (i))
/* full 16-bit tag word (2 bits per physical register: 00 valid, 01 zero, 10 special, 11 empty), derived from the register contents as the CPU does on fnstenv */
static u16 x_tagword(CPU*c) {
    u16 tw = 0;
    for (int p = 0; p < 8; p++) {
        u16 t;
        if (!((c->ftw >> p) & 1)) t = 3;
        else switch (fpclassify(c->st[p])) { case FP_ZERO: t = 1; break; case FP_NORMAL: t = 0; break; default: t = 2; }
        tw |= (u16)(t << (2 * p));
    }
    return tw;
}
static void x_settag(CPU*c, u16 tw) { u8 f = 0; for (int p = 0; p < 8; p++) if (((tw >> (2 * p)) & 3) != 3) f |= (u8)(1u << p); c->ftw = f; }
/* precision control (fcw bits 8-9): 24 / 53 / 64 bit significand, exponent range stays extended. Only FADD/FSUB/FMUL/FDIV (and the reversed / integer forms) and FSQRT honour it.
 * Rounding control (fcw bits 10-11) is honoured by those too, by fst m32/m64, fist and frndint. */
static int x_pcbits(CPU*c) { int pc = (c->fcw >> 8) & 3; return pc == 0 ? 24 : pc == 2 ? 53 : 64; }
/* round to `bits` significant bits (nearest even) in the 80-bit exponent range: denormals have fewer bits (spacing 2^-16445), too large becomes infinity */
static long double x_rb(long double v, int bits) {
    if (v == 0 || !isfinite(v)) return v;
    int e; frexpl(v, &e);
    int sp = e - bits; if (sp < -16445) sp = -16445;
    return ldexpl(nearbyintl(ldexpl(v, -sp)), sp);
}
/* round-to-odd of s given the sign of the rounding error: makes a second rounding to fewer bits a single, correct one */
static long double x_odd(long double s, int up) {
    int e; long double f = frexpl(fabsl(s), &e);
    if (fmodl(ldexpl(f, LDBL_MANT_DIG), 2.0L) != 0) return s;
    return nextafterl(s, up ? INFINITY : -INFINITY);
}
static long double xround(CPU*c, long double v) {
    switch ((c->fcw >> 10) & 3) { case 1: return floorl(v); case 2: return ceill(v); case 3: return truncl(v); default: return nearbyintl(v); }
}
/* rounding control (fcw bits 10-11): 0 nearest even, 1 down, 2 up, 3 toward zero */
static int x_rc(CPU*c) { return (c->fcw >> 10) & 3; }
/* largest finite number with a `bits`-bit significand (the result of an overflow that rounds toward zero) */
static long double x_maxfin(int bits, int neg) {
    long double v = ldexpl((long double)(bits == 64 ? ~0ULL : ((1ULL << bits) - 1)), 16384 - bits); return neg ? -v : v;
}
/* does an overflow in this rounding mode give the largest finite number instead of infinity? */
static int x_noinf(int rc, int neg) { return rc == 3 || (rc == 2 && neg) || (rc == 1 && !neg); }
/* round (s + e) * 2^sc to `bits` significant bits (denormal spacing as the hardware has it) in rounding mode rc (0 nearest even, 1 down, 2 up, 3 toward zero). s is the host's nearest result and e its
 * error (only the sign of e matters, and e is smaller than the host's ulp, so it can only decide when s sits exactly on the grid). The result is in the units of s
 * (the result already includes the 2^sc). *inex: the exact value was not on the grid; *ru: the magnitude was rounded up. */
static long double x_rdir(int rc, long double s, long double e, int sc, int bits, int*inex, int*ru, int clamp) {
    *inex = 0; *ru = 0;
    if (s == 0 || !isfinite(s)) return s;
    int sg = signbit(s) != 0, es; frexpl(s, &es);
    int sp = es + sc - bits; if (clamp && sp < -16381 - bits) sp = -16381 - bits;     /* the denormal grid is 2^(-16382-(bits-1)): 2^-16445 at 64 bits, coarser under a smaller precision control (as the hardware does) */
    int k = sc - sp;                                                     /* grid unit in s units is 2^-k */
    long double ms = fabsl(s), me = sg ? -e : e, lo, hi; int frac;
    if (es + k < -16000) { lo = 0; hi = 1; frac = 1; }                  /* far below the grid (the scaled value is under 2^-16000, where ldexpl would lose it): 0 or one unit. k alone is no test: it is very negative for a huge s too */
    else {
        long double m = ldexpl(ms, k), fl = floorl(m);
        frac = m != fl;
        if (frac) { lo = fl; hi = fl + 1; }
        else { lo = me < 0 ? fl - 1 : fl; hi = me > 0 ? fl + 1 : fl; frac = me != 0; }
    }
    *inex = frac;
    int upm = rc == 2 ? !sg : rc == 1 ? sg : 0;                         /* round the magnitude up? */
    if (rc == 0 && frac) {                                              /* nearest even: compare the exact remainder with one half */
        if (es + k < -16000) upm = 0;
        else {
            long double m = ldexpl(ms, k), fl = floorl(m), d = m - fl;
            if (d == 0) upm = me < 0;                                   /* s is on a grid point and the exact value is a sliver away from it: s itself is the nearest (hi if the exact value is below it) */
            else if (d != 0.5L) upm = d > 0.5L;
            else upm = me > 0 ? 1 : me < 0 ? 0 : (fmodl(fl, 2.0L) != 0);
        }
    }
    long double q = (frac && upm) ? hi : lo;
    *ru = frac && upm;
    long double r = ldexpl(q, sp);                                      /* the true value: a multiple of the grid, so exact unless it overflows */
    return sg ? -r : r;
}
/* ---- fast path for + - * / on normal operands: exact integer arithmetic on the significands, correctly rounded to the precision control (24/53/64 bits),
 * PE and C1 straight from the rounding. Independent of the host's long double (80-bit or quad), so there is no double rounding and no error-term arithmetic.
 * Anything else (zero, denormal, infinity, NaN, a result outside the normal range) returns 0 and takes the general path below. */
typedef unsigned __int128 xu128;
static int x_unpack(long double v, u64*m, int*e, int*neg) {
#if X87_NATIVE
    u16 se; memcpy(m, &v, 8); memcpy(&se, (const char*) &v + 8, 2);
    *neg = se >> 15; *e = se & 0x7fff; return *e >= 1 && *e <= 0x7ffe && (*m >> 63);
#elif LDBL_MANT_DIG == 113
    u64 q[2]; memcpy(q, &v, 16);
    *neg = (int)(q[1] >> 63); *e = (int)((q[1] >> 48) & 0x7fff);
    if (*e < 1 || *e > 0x7ffe || (q[0] & ((1ULL << 49) - 1))) return 0;            /* bits below the 64-bit significand: not a guest value, take the general path */
    *m = (1ULL << 63) | ((q[1] & 0xffffffffffffULL) << 15) | (q[0] >> 49); return 1;
#else
    (void) v; (void) m; (void) e; (void) neg; return 0;
#endif
}
static long double x_pack(u64 m, int e, int neg) {
#if X87_NATIVE
    long double v = 0; u16 se = (u16)((neg << 15) | e); memcpy(&v, &m, 8); memcpy((char*) &v + 8, &se, 2); return v;
#else
    u64 q[2]; q[0] = m << 49; q[1] = ((u64) neg << 63) | ((u64) e << 48) | ((m & 0x7fffffffffffffffULL) >> 15);
    long double v; memcpy(&v, q, 16); return v;
#endif
}
static int x_fast(CPU*c, int k, long double a, long double b, long double*out) {
    u64 ma, mb; int ea, eb, na, nb;
    if (!x_unpack(a, &ma, &ea, &na) || !x_unpack(b, &mb, &eb, &nb)) return 0;
    xu128 w; int E, neg, sticky = 0;                                          /* value = w / 2^127 * 2^E, E unbiased; w has bit 127 set */
    if (k == 1) {
        xu128 p = (xu128) ma * mb; E = ea + eb - 2 * 16383; neg = na ^ nb;
        if (p >> 127) E++; else p <<= 1;
        w = p;
    } else if (k == 6) {
        neg = na ^ nb; E = ea - eb;
        xu128 n1 = ma >= mb ? ((xu128) ma << 63) : ((xu128) ma << 64);
        if (ma < mb) E--;
        xu128 qh = n1 / mb, r1 = n1 % mb, ql = (r1 << 64) / mb, r2 = (r1 << 64) % mb;
        w = (qh << 64) | ql; sticky = r2 != 0;
    } else {                                                                  /* k == 0: a + b (a - b is passed as a + (-b)) */
        if (k == 0) { }
        int sb = nb;                                                          /* effective sign of b */
        xu128 A, B; int d, ne; 
        if (ea > eb || (ea == eb && ma >= mb)) { A = (xu128) ma << 64; B = (xu128) mb << 64; d = ea - eb; E = ea - 16383; neg = na; ne = nb; }
        else { A = (xu128) mb << 64; B = (xu128) ma << 64; d = eb - ea; E = eb - 16383; neg = nb; ne = na; }
        (void) sb;
        if (d >= 128) { B = 0; sticky = 1; } else if (d) { sticky = (B & (((xu128) 1 << d) - 1)) != 0; B >>= d; }
        if (ne == neg) {
            w = A + B;
            if (w < A) { sticky |= (int)(w & 1); w = (w >> 1) | ((xu128) 1 << 127); E++; }
        } else {
            w = A - B - (sticky ? 1 : 0);
            if (!w && !sticky) return 0;                                      /* exact zero: sign rules, general path */
            if (!(w >> 64)) { if (!w) return 0; }
            int lz = 0; xu128 t = w; while (!(t >> 127)) { t <<= 1; lz++; }    /* normalise (cancellation is exact: no sticky bits are in play then) */
            w = t; E -= lz;
        }
    }
    int P = x_pcbits(c), sh = 128 - P, rc = x_rc(c);
    if (sticky) w |= 1;
    xu128 rem = w & (((xu128) 1 << sh) - 1), half = (xu128) 1 << (sh - 1), t = w >> sh; int up = 0, inex = rem != 0;
    int be0 = E + 16383;                                                      /* biased exponent before rounding: tininess is judged on it */
    int bump = rc == 0 ? (rem > half || (rem == half && (t & 1))) : (inex && (rc == 2 ? !neg : rc == 1 ? neg : 0));      /* nearest even / down / up / toward zero, on the magnitude */
    if (bump) { t++; up = 1; if (t >> P) { t >>= 1; E++; } }
    int be = E + 16383, ex = 0;
    if (be > 0x7ffe && !(c->fcw & X_OE)) { be -= 24576; ex = X_OE; }          /* unmasked OE: the rounded result with the exponent brought back by 2^-24576 */
    else if (be0 < 1 && !(c->fcw & X_UE)) { be += 24576; ex = X_UE; }         /* unmasked UE (the result is tiny, exact or not): exponent + 2^24576 */
    else if (be < 1 || be > 0x7ffe) return 0;
    if (ex && (be < 1 || be > 0x7ffe)) return 0;
    *out = x_pack((u64)(t << (64 - P)), be, neg);
    c->fsw = (u16)((c->fsw & ~X_C1) | (up ? X_C1 : 0));
    if (inex) c->fsw |= X_PE;
    if (ex) c->fsw |= (u16) ex;
    return 1;
}
/* a op b with the xop codes (0 add, 1 mul, 4 a-b, 5 b-a, 6 a/b, 7 b/a): result under precision control, all exception flags, C1 = rounded up */
static long double x_arith(CPU*c, int k, long double a, long double b) {
    if (k == 5) { long double t = a; a = b; b = t; k = 4; } else if (k == 7) { long double t = a; a = b; b = t; k = 6; }
    { long double fr; if (x_fast(c, k, a, k == 4 ? -b : b, &fr)) return fr; }
    if (isnan(a) || isnan(b)) { if (x_issnan(a) || x_issnan(b)) x_exc(c, X_IE); c->fsw &= ~X_C1; return x_nanpick(a, b); }
    int fl = (x_den(a) || x_den(b)) ? X_DE : 0;
    if (k == 4) { b = -b; k = 0; }
    int inv;
    switch (k) {
    case 0: inv = isinf(a) && isinf(b) && signbit(a) != signbit(b); break;
    case 1: inv = (isinf(a) && b == 0) || (isinf(b) && a == 0); break;
    default: inv = (a == 0 && b == 0) || (isinf(a) && isinf(b)); break;
    }
    if (inv) { x_exc(c, X_IE); c->fsw &= ~X_C1; return x_indef(); }
    if (k == 6 && b == 0 && a != 0 && !isinf(a)) fl = X_ZE;                 /* a divide by zero does not also report the denormal operand */
    long double s, e = 0, r; int up = 0, inex, ru = 0;                      /* exact = s + e; up: e is positive in the direction of the magnitude (quotient: remainder scaled by 1/b) */
    int rc = x_rc(c);
    if (k == 0) { s = a + b; if (isfinite(s)) { long double bb = s - a; e = (a - (s - bb)) + (b - bb); } up = e > 0;
        if (rc == 1 && s == 0 && isfinite(a) && isfinite(b) && signbit(a) != signbit(b)) s = -0.0L; }        /* x + (-x) is -0 when rounding down */
    else if (k == 1) { s = a * b; if (isfinite(s)) e = fmal(a, b, -s); up = e > 0; }
    else { s = a / b; if (isfinite(s) && isfinite(b) && b != 0 && isfinite(a)) { e = fmal(-s, b, a); up = (e > 0) != (b < 0); } }
    if (k != 0 && (isfinite(s) || !(c->fcw & X_OE)) && isfinite(a) && isfinite(b) && a != 0 && b != 0 && (!isfinite(s) || fabsl(s) < 0x1p-16300L || fabsl(a) < 0x1p-16300L || fabsl(b) < 0x1p-16300L)) {
        /* tiny operands or result: the error term of the host operation is not representable down here, so run the operation on the significands
         * (|f| in [0.5, 1): everything is normal), then scale back; the result is rounded once onto the final (possibly denormal) grid by x_rdir;
         * underflow is judged before rounding, as the CPU does. */
        int ea, eb; long double fa = frexpl(a, &ea), fb = frexpl(b, &eb), p1, e1; int up1, sc;
        if (k == 1) { p1 = fa * fb; e1 = fmal(fa, fb, -p1); up1 = e1 > 0; sc = ea + eb; }
        else { p1 = fa / fb; e1 = fmal(-p1, fb, fa); up1 = (e1 > 0) != (fb < 0); sc = ea - eb; }
        long double t = ldexpl(p1, sc + 16382);                              /* p1 * 2^sc in units of the smallest normal number */
        int tiny = fabsl(t) < 1 || (fabsl(t) == 1 && e1 != 0 && (up1 != 0) != (p1 > 0));
        int pcb = x_pcbits(c);
        {                                                                    /* one rounding of the exact value onto the final grid: the CPU's denormal grid depends on the precision control */
            long double ee = (k == 1 || e1 == 0) ? e1 : (((e1 > 0) == (fb > 0)) ? 1.0L : -1.0L);
            int ix, rr; s = x_rdir(rc, p1, ee, sc, pcb, &ix, &rr, 1); inex = ix; ru = rr;
            { int ex, e0; frexpl(p1, &e0); long double sa = 0; if (e0 + sc > 16300 && !(c->fcw & X_OE)) { sa = x_rdir(rc, p1, ee, sc - 24576, pcb, &ix, &rr, 0); frexpl(sa, &ex); } else ex = -100000;
              if (ex + 24576 > 16384) { c->fsw = (u16)((c->fsw & ~X_C1) | (rr ? X_C1 : 0)); x_exc(c, fl | X_OE | (ix ? X_PE : 0)); return sa; } }       /* unmasked OE */
            if (tiny && !(c->fcw & X_UE)) { long double sb = x_rdir(rc, p1, ee, sc + 24576, pcb, &ix, &rr, 0);
              c->fsw = (u16)((c->fsw & ~X_C1) | (rr ? X_C1 : 0)); x_exc(c, fl | X_UE | (ix ? X_PE : 0)); return sb; }                                           /* unmasked UE */
        }
        if (isinf(s)) { fl |= X_OE | X_PE; ru = 1; if (x_noinf(rc, signbit(s))) { s = x_maxfin(pcb, signbit(s)); ru = 0; } }
        else if (inex) { fl |= X_PE; if (tiny) fl |= X_UE; }
        c->fsw = (u16)((c->fsw & ~X_C1) | (ru ? X_C1 : 0)); x_exc(c, fl);
        return s;
    }
    r = s; int pcb = x_pcbits(c);
    if ((rc || fabsl(s) < 0x1p-16300L) && isfinite(s) && s != 0) {                 /* directed rounding, or a result near the denormal range (its grid depends on the precision control) */
        long double ee = (k == 6 && e != 0) ? (((e > 0) == (b > 0)) ? 1.0L : -1.0L) : e; int ix, rr;
        r = x_rdir(rc, s, ee, 0, pcb, &ix, &rr, 1); inex = ix; ru = rr;
    } else {
    if ((X87_WIDE || pcb < 64) && isfinite(s)) { long double so = (e != 0 && LDBL_MANT_DIG > pcb + 1) ? x_odd(s, up) : s; r = x_rb(so, pcb); }
    inex = e != 0 || r != s;
    if (inex && isfinite(s)) { if (r != s) ru = fabsl(r) > fabsl(s); else ru = s != 0 && e != 0 && ((up != 0) != (s > 0)); }
    else if (isinf(r) && isfinite(a) && isfinite(b) && !(k == 6 && b == 0)) ru = 1;
    }
    if (isinf(r) && isfinite(a) && isfinite(b) && !(k == 6 && b == 0) && k == 0 && isfinite(s) && !(c->fcw & X_OE)) {
        /* unmasked OE of an addition (a denormal operand keeps it off the fast path): the rounded result (bits of the precision control, exponent unbounded) with the exponent
         * brought back by 2^-24576. The rounding that overflowed was up in magnitude, so the same rounding at the smaller scale gives the significand. */
        int ix, rr; long double sa = x_rdir(rc, s, e, -24576, pcb, &ix, &rr, 0);
        c->fsw = (u16)((c->fsw & ~X_C1) | (rr ? X_C1 : 0)); x_exc(c, fl | X_OE | (ix ? X_PE : 0));
        return sa;
    }
    if (isinf(r) && isfinite(a) && isfinite(b) && !(k == 6 && b == 0)) {
        fl |= X_OE | X_PE; ru = 1;
        if (x_noinf(rc, signbit(r))) { r = x_maxfin(pcb, signbit(r)); ru = 0; }
    }
    else if (inex) { fl |= X_PE; if (x_den(r)) fl |= X_UE; }
    c->fsw = (u16)((c->fsw & ~X_C1) | (ru ? X_C1 : 0)); x_exc(c, fl);
    return r;
}
static long double x_sqrt(CPU*c, long double a) {
    if (isnan(a)) { if (x_issnan(a)) x_exc(c, X_IE); c->fsw &= ~X_C1; return x_quiet(a); }
    if (a < 0) { x_exc(c, X_IE); c->fsw &= ~X_C1; return x_indef(); }
    if (x_den(a)) x_exc(c, X_DE);
    long double s, e = 0, r, s1 = 0; int sc = 0, rc = x_rc(c);
    if (a != 0 && isfinite(a) && a < 0x1p-16000L) {                         /* tiny: scale by 2^(2K) (exact, the root scales by 2^K) so the error term is representable */
        long double a1 = ldexpl(a, 16600); s1 = sqrtl(a1); e = fmal(-s1, s1, a1); s = ldexpl(s1, -8300); sc = -8300;
    } else { s = sqrtl(a); r = s; if (isfinite(s) && s != 0) e = fmal(-s, s, a); s1 = s; }           /* a - s*s: > 0 means s was rounded down */
    r = s;
    int pcb = x_pcbits(c), inex, ru = 0;
    if (rc && isfinite(s) && s != 0) { r = x_rdir(rc, s1, e, sc, pcb, &inex, &ru, 1); }
    else {
    if ((X87_WIDE || pcb < 64) && isfinite(s) && s != 0) r = x_rb((e != 0 && LDBL_MANT_DIG > pcb + 1) ? x_odd(s, e > 0) : s, pcb);
    inex = e != 0 || r != s;
    if (inex) ru = r != s ? fabsl(r) > fabsl(s) : e < 0;
    }
    if (inex) x_exc(c, X_PE);
    c->fsw = (u16)((c->fsw & ~X_C1) | (ru ? X_C1 : 0));
    return r;
}
/* IE / ZE / DE are detected before the operation computes anything: when one of them is unmasked the destination (and the stack) stay as they were and the
 * exception is raised by the next waiting instruction. x_begin saves the sticky flags and clears them so that x_end sees only what this instruction raised;
 * x_end merges them back and returns 1 if the result must not be stored. */
static u16 x_begin(CPU*c) { u16 o = c->fsw & 0x7f; c->fsw &= (u16) ~0x7f; return o; }
static int x_end(CPU*c, u16 o) {
    u16 n = c->fsw & 0x7f; int ab = (n & 7 & ~c->fcw) != 0;
    if (ab) { c->fsw = (u16)((c->fsw & ~0x7f) | (n & 0x47)); if (!(n & X_SF)) c->fsw &= ~X_C1; }        /* only the exception that stopped it is reported: no OE / UE / PE, no rounding C1 */
    c->fsw |= o; return ab;
}
/* a store to memory (fst m32 / m64, fist, fisttp) with an unmasked exception in `mask` (IE for the integer stores, IE | OE | UE for fst) writes nothing and does not pop;
 * only the flags of that exception are reported. Otherwise like x_end. */
static int x_endm(CPU*c, u16 o, int mask) {
    u16 n = c->fsw & 0x7f; int ab = (n & mask & ~c->fcw) != 0;
    if (ab) { c->fsw = (u16)((c->fsw & ~0x7f) | (n & 0x5f & ~X_PE)); if (!(n & X_SF)) c->fsw &= ~X_C1; }
    c->fsw |= o; return ab;
}
static long double xop(CPU*c, int r, long double a, long double b) { return x_arith(c, r, a, b); }
/* fcom / fucom family: ordered = 1 raises IE for any NaN, 0 only for a signalling NaN */
static void x_cc(CPU*c, long double a, long double b, int ordered) {
    c->fsw &= ~0x4700;
    if (isnan(a) || isnan(b)) { c->fsw |= 0x4500; if (ordered || x_issnan(a) || x_issnan(b)) x_exc(c, X_IE); return; }
    if (x_den(a) || x_den(b)) x_exc(c, X_DE);
    if (a < b) c->fsw |= 0x0100; else if (a == b) c->fsw |= 0x4000;
}
static void x_flags(CPU*c, long double a, long double b, int ordered) {
    RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
    c->fsw &= ~X_C1;
    if (isnan(a) || isnan(b)) { RF(c) |= ZF_ | PF_ | CF_; if (ordered || x_issnan(a) || x_issnan(b)) x_exc(c, X_IE); return; }
    if (x_den(a) || x_den(b)) x_exc(c, X_DE);
    if (a < b) RF(c) |= CF_; else if (a == b) RF(c) |= ZF_;
}
/* fist / fisttp: trunc = 1 truncates whatever the rounding mode says. Out of range or NaN: IE and the integer indefinite; a rounded value: PE (C1 = rounded up) */
static u64 x_toint(CPU*c, long double v, int sz, int trunc) {
    long double lim = sz == 2 ? 32768.0L : sz == 4 ? 2147483648.0L : 9223372036854775808.0L;
    u64 ind = sz == 2 ? 0x8000 : sz == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    if (isnan(v)) { x_exc(c, X_IE); return ind; }
    long double r = trunc ? truncl(v) : xround(c, v);
    if (r >= lim || r < -lim) { x_exc(c, X_IE); return ind; }
    if (r != v) { x_exc(c, X_PE); if (fabsl(r) > fabsl(v)) c->fsw |= X_C1; }
    return (u64)(s64) r;
}
/* fst m32 / m64: NaN keeps its high payload bits (quieted), otherwise the host conversion; OE UE PE and C1 from the rounding */
static void x_conv_flags(CPU*c, long double v, long double back, long double tiny, long double lim) {
    if (!isfinite(v)) return;
    if (!isinf(back) && fabsl(v) >= lim) { x_exc(c, X_OE | X_PE); c->fsw &= ~X_C1; return; }     /* overflow that rounds to the largest finite number (rounding toward zero) */
    if (v != 0 && fabsl(v) < tiny && !(c->fcw & X_UE)) x_exc(c, X_UE);                        /* unmasked UE: reported for a tiny value even if it is exact */
    if (back == v) return;
    if (isinf(back)) { x_exc(c, X_OE | X_PE); c->fsw |= X_C1; return; }
    x_exc(c, X_PE | (fabsl(v) < tiny ? X_UE : 0));
    if (fabsl(back) > fabsl(v)) c->fsw |= X_C1;
}
/* the host conversion under the guest's rounding control */
static int x_hostmode(CPU*c) { static const int m[4] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }; return m[x_rc(c)]; }
static float x_to_f(CPU*c, long double v) {
    if (isnan(v)) { u8 b[10]; if (x_issnan(v)) x_exc(c, X_IE); ld_to80(v, b); u32 f = (u32)((b[9] & 0x80) << 24) | 0x7fc00000u | (u32)(((*(u64u*) b) >> 40) & 0x3fffff); float o; memcpy(&o, &f, 4); return o; }
    volatile long double vv = v; int old = fegetround(); fesetround(x_hostmode(c)); volatile float f = (float) vv; fesetround(old);
    x_conv_flags(c, v, (long double) f, 1.17549435082228750797e-38L, 0x1p128L); return f;
}
static double x_to_d(CPU*c, long double v) {
    if (isnan(v)) { u8 b[10]; if (x_issnan(v)) x_exc(c, X_IE); ld_to80(v, b); u64 f = ((u64)(b[9] & 0x80) << 56) | 0x7ff8000000000000ULL | (((*(u64u*) b) >> 11) & 0x7ffffffffffffULL); double o; memcpy(&o, &f, 8); return o; }
    volatile long double vv = v; int old = fegetround(); fesetround(x_hostmode(c)); volatile double d = (double) vv; fesetround(old);
    x_conv_flags(c, v, (long double) d, 2.2250738585072013831e-308L, 0x1p1024L); return d;
}
/* ---- extended precision helpers for the transcendental instructions: the result is computed in (at least) 113 bits and rounded once to 64 */
#if defined(__SIZEOF_FLOAT128__) && X87_NATIVE
typedef __float128 xq;
#define XQC(c) c##Q
#else
typedef long double xq;
#define XQC(c) c##L
#endif
#define XQ_LN2   XQC(0.693147180559945309417232121458176568075500134360255254120680009)
#define XQ_LOG2E XQC(1.442695040888963407359924681001892137426645954152985934135449407)
static xq q_abs(xq v) { return v < 0 ? -v : v; }
/* 2*atanh(s) for |s| < 0.18 */
static xq q_atanh2(xq s) {
    xq s2 = s * s, t = s, sum = 0;
    for (int k = 1; k < 90; k += 2) { xq term = t / (xq) k; sum += term; t *= s2; if (q_abs(term) < q_abs(sum) * XQC(1e-36)) break; }
    return 2 * sum;
}
static xq q_log2(long double x) {                       /* x finite, > 0 */
    int e; long double f = frexpl(x, &e); xq m = (xq) f;
    if (m < XQC(0.70710678118654752440084436210484903928)) { m *= 2; e--; }
    xq s = (m - 1) / (m + 1);
    return (xq) e + q_atanh2(s) * XQ_LOG2E;
}
static xq q_log2p1(long double x) {                     /* log2(1 + x), x > -1 */
    if (fabsl(x) < 0.3L) { xq s = (xq) x / (2 + (xq) x); return q_atanh2(s) * XQ_LOG2E; }
    xq y = 1 + (xq) x; int e = 0;                       /* 1 + x is exact in a 113-bit number for |x| >= 0.3 */
    while (y >= 2) { y /= 2; e++; }
    while (y < XQC(0.70710678118654752440084436210484903928)) { y *= 2; e--; }
    if (y >= XQC(1.41421356237309504880168872420969807857)) { y /= 2; e++; }
    return (xq) e + q_atanh2((y - 1) / (y + 1)) * XQ_LOG2E;
}
static xq q_exp2m1(long double x) {                     /* 2^x - 1, |x| <= 1 */
    xq z = (xq) x * XQ_LN2, t = z, sum = z;
    for (int k = 2; k < 60; k++) { t = t * z / (xq) k; sum += t; if (q_abs(t) < q_abs(sum) * XQC(1e-36)) break; }
    return sum;
}
static void q_sincos(xq r, xq*sn, xq*cs) {             /* |r| <= pi/4 + epsilon */
    xq r2 = r * r, t = r, s = r, u = 1, co = 1;
    for (int k = 1; k < 40; k++) {
        t = -t * r2 / (xq)((2 * k) * (2 * k + 1)); s += t;
        u = -u * r2 / (xq)((2 * k - 1) * (2 * k)); co += u;
        if (q_abs(t) < XQC(1e-40) && q_abs(u) < XQC(1e-40)) break;
    }
    *sn = s; *cs = co;
}
/* argument reduction like the CPU: with a 66-bit pi (0x3243F6A8885A308D3 * 2^-64), so the result near multiples of pi differs from libm exactly as the
 * hardware does. |x| < 2^63; returns the reduced argument (|r| <= pi/4) and the quadrant (n mod 4). */
static xq x_reduce(long double ax, unsigned*quad) {
    if (ax < 0.78539816339744830961L) { *quad = 0; return (xq) ax; }
    int e; long double f = frexpl(ax, &e);
    u64 m = (u64) ldexpl(f, 64); int E = e - 64;
    unsigned __int128 xs = (unsigned __int128) m << (E + 65);          /* x in units of 2^-65 (wraps harmlessly: only the small difference is used) */
    const unsigned __int128 P2 = ((unsigned __int128) 3 << 64) | 0x243F6A8885A308D3ULL;
    u64 n = (u64) rintl(ax / 1.57079632679489661923L);
    __int128 r = (__int128)(xs - (unsigned __int128) n * P2), hp = (__int128)(P2 >> 1);
    while (r > hp) { r -= (__int128) P2; n++; }
    while (r < -hp) { r += (__int128) P2; n--; }
    *quad = (unsigned)(n & 3);
    xq q = (xq) r;
    return q / ((xq)(1ULL << 63) * 4);
}
/* the result r (long double) of a quad computation: PE if rounded, UE if it landed in the denormal range, OE if it overflowed; C1 = rounded up */
static long double x_fin(CPU*c, xq q) {
    long double r = (long double) q;
    int rc = x_rc(c);
    if (rc && isfinite(r) && r != 0) {                                 /* directed rounding: move to the neighbour on the right side of the exact (quad) value */
        xq d = q - (xq) r;                                             /* exact; zero when the host has no wider type than long double */
        if (d != 0) {
            int neg = r < 0;
            if (rc == 1 && d < 0) r = nextafterl(r, -INFINITY);
            else if (rc == 2 && d > 0) r = nextafterl(r, INFINITY);
            else if (rc == 3 && ((!neg && d < 0) || (neg && d > 0))) r = nextafterl(r, 0.0L);
        }
    }
    if (isinf(r)) { x_exc(c, X_OE | X_PE); c->fsw |= X_C1; return r; }
    if (isfinite(r) && (xq) r != q) { x_exc(c, X_PE | ((r == 0 || x_den(r)) ? X_UE : 0)); if (q_abs((xq) r) > q_abs(q)) c->fsw |= X_C1; }
    return r;
}
/* fsin (0), fcos (1), fptan (2), fsincos (3): returns 0 if |x| >= 2^63 (C2 = 1, nothing changes) */
static int x_trig(CPU*c, long double x, int which, long double*o1, long double*o2) {
    if (isnan(x)) { if (x_issnan(x)) x_exc(c, X_IE); *o1 = *o2 = x_quiet(x); c->fsw &= ~0x400; return which == 2 ? 2 : 1; }
    if (isinf(x)) { x_exc(c, X_IE); *o1 = *o2 = x_indef(); c->fsw &= ~0x400; return which == 2 ? 2 : 1; }
    long double ax = fabsl(x);
    if (ax >= 9223372036854775808.0L) { c->fsw |= 0x400; return 0; }
    c->fsw &= ~0x400;
    if (x_den(x)) x_exc(c, X_DE);
    if (x == 0) { *o1 = which == 1 ? 1.0L : x; *o2 = 1.0L; return 1; }
    unsigned qd; xq r = x_reduce(ax, &qd), sn, cs;
    q_sincos(r, &sn, &cs);
    xq S, C;                                                           /* sin(|x|), cos(|x|) from the quadrant */
    switch (qd) { case 0: S = sn; C = cs; break; case 1: S = cs; C = -sn; break; case 2: S = -sn; C = -cs; break; default: S = -cs; C = sn; break; }
    if (signbit(x)) S = -S;                                           /* now sin(x), cos(x) */
    if (which == 0) { *o1 = x_fin(c, S); if (ax < 0x1p-32L) x_exc(c, X_PE | (x_den(x) ? X_UE : 0)); }                 /* sin(tiny) = x - x^3/6: inexact */
    else if (which == 1) { *o1 = x_fin(c, C); if (C == 1) x_exc(c, X_PE); }                  /* cos(tiny) = 1 - eps is inexact even where the quad sum cannot show it */
    else if (which == 3) { *o1 = x_fin(c, S); *o2 = x_fin(c, C); if (C == 1) x_exc(c, X_PE); if (ax < 0x1p-32L) x_exc(c, X_PE | (x_den(x) ? X_UE : 0)); }
    else { *o1 = x_fin(c, S / C); if (ax < 0x1p-32L) x_exc(c, X_PE | (x_den(x) ? X_UE : 0)); }
    return 1;
}
/* fprem (trunc = 1) / fprem1 (trunc = 0): complete reduction, C2 = 0, the low three quotient bits in C0 (Q2), C3 (Q1), C1 (Q0) */
static long double x_prem(CPU*c, long double a, long double b, int trunc) {
    c->fsw &= ~0x4700;
    if (isnan(a) || isnan(b)) { if (x_issnan(a) || x_issnan(b)) x_exc(c, X_IE); return x_nanpick(a, b); }
    if (isinf(a) || b == 0) { x_exc(c, X_IE); return x_indef(); }
    if (x_den(a) || x_den(b)) x_exc(c, X_DE);
    if (a == 0) return a;
    int ea, eb; frexpl(a, &ea); frexpl(b, &eb);
    if (isfinite(b) && ea - eb >= 64) {                              /* exponents 64 or more apart: a partial reduction (C2 = 1, run it again), by a multiple of b * 2^(d - n), n = 32..63 */
        int d = ea - eb, n = 32 + (d % 32);
        long double bs = ldexpl(b, d - n), q = truncl(a / bs), rr = fmal(-q, bs, a), sg = a < 0 ? -1.0L : 1.0L;
        if (rr != 0 && (rr < 0) != (a < 0)) { q -= sg; rr = fmal(-q, bs, a); } else if (fabsl(rr) >= fabsl(bs)) { q += sg; rr = fmal(-q, bs, a); }
        c->fsw |= 0x400;
        return rr == 0 ? copysignl(0.0L, a) : rr;
    }
    long double r = trunc ? fmodl(a, b) : remainderl(a, b);
    if (r == 0) r = copysignl(0.0L, a);
    if (isfinite(b)) {
        long double qf = rintl((a - r) / b); unsigned q = (unsigned) (u64) fabsl(fmodl(qf, 8.0L));
        if (q & 4) c->fsw |= 0x100;
        if (q & 2) c->fsw |= 0x4000;
        if (q & 1) c->fsw |= 0x200;
    }
    return r;
}
#define XQ_PI2 XQC(1.5707963267948966192313216916397514420985846996875529104874722961)
static void q_sincos_any(xq t, xq*sn, xq*cs) {                  /* |t| <= 4 */
    long n = (long)(t / XQ_PI2 + (t < 0 ? -XQC(0.5) : XQC(0.5)));
    xq s, k; q_sincos(t - (xq) n * XQ_PI2, &s, &k);
    switch (((n % 4) + 4) % 4) { case 0: *sn = s; *cs = k; break; case 1: *sn = k; *cs = -s; break; case 2: *sn = -s; *cs = -k; break; default: *sn = -k; *cs = s; break; }
}
/* fpatan: atan2l gives the value to within an ulp or so; a Newton step in quad precision makes it (nearly) exact, and the result is then rounded once in the rounding mode of the
 * control word (x_rdir) with PE, UE and C1 = rounded up, as the CPU does. The multiples of pi/4 (zero or infinite arguments) come from the quad constant. A tiny quotient
 * (|y/x| < ~2^-33, x > 0) is computed on the significands and scaled, because it may be below what quad can hold (the result is then in or near the denormal range). */
static long double x_patan(CPU*c, long double y, long double x) {
    long double r = atan2l(y, x);
    int k = 0, neg = signbit(y) != 0, rc = x_rc(c);
    if (y == 0) { if (!signbit(x)) return r; k = 4; }                                /* +-0 for x >= +0 is exact; x <= -0 gives +-pi */
    else if (x == 0) k = 2;
    else if (isinf(y)) k = isinf(x) ? (x > 0 ? 1 : 3) : 2;
    else if (isinf(x)) { if (x > 0) return r; k = 4; }
    long double s, e; int sc = 0, ix, ru, tiny = 0, always = 0;
    if (k) { xq t = (xq) k * (XQ_PI2 / 2); s = (long double) t; e = (long double)(t - (xq) s); }       /* the magnitude: pi/4, pi/2, 3pi/4 or pi */
    else if (x > 0 && fabsl(r) < 0x1p-33L) {
        int ey, ex; long double fy = fabsl(frexpl(y, &ey)), fx = frexpl(x, &ex);
        xq zs = (xq) fy / (xq) fx; int ez = ey - ex;
        xq t = zs;                                                                    /* the CPU returns z itself here (no correction below it), always inexact */
        s = (long double) t; e = (long double)(t - (xq) s);
        sc = ez; tiny = fabsl(ldexpl(s, ez)) < 0x1p-16382L; always = 1;
    } else {
        xq xs, ys, sn, cs; int kk; frexpl(fmaxl(fabsl(x), fabsl(y)), &kk); xs = (xq) ldexpl(fabsl(x), -kk); ys = (xq) ldexpl(fabsl(y), -kk);
        xq t = (xq) fabsl(atan2l(fabsl(y), fabsl(x)));                               /* the angle of (|x|, |y|) in the first quadrant (one Newton step), folded back for x < 0 */
        q_sincos_any(t, &sn, &cs); { xq f = sn * xs - cs * ys, d = cs * xs + sn * ys; if (d != 0) t -= f / d; }
        if (x < 0) t = (xq) 2 * XQ_PI2 - t;
        s = (long double) t; e = (long double)(t - (xq) s);
    }
    int rcm = rc; if (neg && rc == 1) rcm = 2; else if (neg && rc == 2) rcm = 1;      /* s is a magnitude: down for a negative result is up for the magnitude */
    long double q = x_rdir(rcm, s, e, sc, 64, &ix, &ru, 1);
    c->fsw &= ~X_C1;
    if (ix || always) { x_exc(c, X_PE | (tiny ? X_UE : 0)); if (ru) c->fsw |= X_C1; }
    return neg ? -q : q;
}
/* fyl2x (p1 = 0: y * log2(x)) and fyl2xp1 (p1 = 1: y * log2(x + 1)) */
static long double x_yl2x(CPU*c, long double x, long double y, int p1) {
    if (isnan(x) || isnan(y)) { if (x_issnan(x) || x_issnan(y)) x_exc(c, X_IE); return x_nanpick(x, y); }
    int neg = p1 ? x < -1 : (x < 0), tzero = p1 ? x == -1 : x == 0, tone = p1 ? x == 0 : x == 1, tinf = x > 0 && isinf(x);
    if (neg) { x_exc(c, X_IE); return x_indef(); }
    if (!tzero && (x_den(x) || x_den(y))) x_exc(c, X_DE);
    if (isinf(y)) { if (tone) { x_exc(c, X_IE); return x_indef(); } int lg = tinf || (p1 ? x > 0 : x > 1); return (y > 0) == lg ? INFINITY : -INFINITY; }
    if (tzero) { if (y == 0) { x_exc(c, X_IE); return x_indef(); } x_exc(c, X_ZE); return y > 0 ? -INFINITY : INFINITY; }
    if (tinf) { if (y == 0) { x_exc(c, X_IE); return x_indef(); } return y > 0 ? INFINITY : -INFINITY; }
    if (tone) return p1 ? y * x : y * 0.0L;
    if (x_den(x) && y != 0) x_exc(c, X_PE);                             /* the CPU reports the denormal case as inexact even when log2 is exact */
    return x_fin(c, (xq) y * (p1 ? q_log2p1(x) : q_log2(x)));
}
static long double x_scale(CPU*c, long double v, long double n) {
    if (isnan(v) || isnan(n)) { if (x_issnan(v) || x_issnan(n)) x_exc(c, X_IE); return x_nanpick(v, n); }
    if (x_den(v) || x_den(n)) x_exc(c, X_DE);
    if (isinf(n)) { if ((n < 0 && isinf(v)) || (n > 0 && v == 0)) { x_exc(c, X_IE); return x_indef(); } return n > 0 ? (v == 0 ? v : copysignl(INFINITY, v)) : copysignl(0.0L, v); }
    long double k = truncl(n); int ki = k > 100000 ? 100000 : k < -100000 ? -100000 : (int) k;
    long double r = ldexpl(v, ki);
    if (isfinite(v) && v != 0) {
        if (isinf(r)) { x_exc(c, X_OE | X_PE); c->fsw |= X_C1; }
        else if ((r == 0 || x_den(r)) && ldexpl(r, -ki) != v) { x_exc(c, X_UE | X_PE); if (fabsl(ldexpl(r, -ki)) > fabsl(v)) c->fsw |= X_C1; }
    }
    return r;
}
static long double x_rint(CPU*c, long double v) {
    if (isnan(v)) { if (x_issnan(v)) x_exc(c, X_IE); return x_quiet(v); }
    if (x_den(v)) x_exc(c, X_DE);
    long double r = xround(c, v);
    if (r != v && isfinite(v)) { x_exc(c, X_PE); if (fabsl(r) > fabsl(v)) c->fsw |= X_C1; }
    return r;
}
static int x87(CPU*c, I*in, u8 op) {
    M m; modrm(c, in, &m, 0);
    int r = m.reg & 7, i = m.rm & 7;
    /* the control instructions that neither wait for nor raise a pending unmasked exception: fldenv fldcw fnstenv fnstcw, frstor fnsave fnstsw, fnclex fninit (and the 8087 nops), fnstsw ax */
    int nowait = m.ismem ? ((op == 0xD9 && r >= 4) || (op == 0xDD && (r == 4 || r >= 6))) : ((op == 0xDB && r == 4) || (op == 0xDF && r == 4 && i == 0));
    if (!nowait) { if (x_pend(c)) return x_trap(c); c->fsw &= ~X_C1; }
    if (m.ismem) {
        u64 ea = m.ea; long double v;
        switch (op) {
        case 0xD8: case 0xDC: case 0xDA: case 0xDE: {
            u16 o = x_begin(c);
            if (op == 0xD8) { float f; memcpy(&f, (void*) ea, 4); v = x_from_f(c, f); }
            else if (op == 0xDC) { double d; memcpy(&d, (void*) ea, 8); v = x_from_d(c, d); }
            else if (op == 0xDA) v = (long double)(s32) mrd(ea, 4);
            else v = (long double)(s16) mrd(ea, 2);
            if (r == 2 || r == 3) { x_cc(c, SR(0), v, 1); if (x_end(c, o)) return 0; if (r == 3) xpop(c); }
            else { long double q = xop(c, r, SR(0), v); if (x_end(c, o)) return 0; ST(0) = q; }
            return 0; }
        case 0xD9:
            switch (r) {
            case 0: { float f; memcpy(&f, (void*) ea, 4); xpush(c, x_from_f(c, f)); return 0; }
            case 2: case 3: { u16 o = x_begin(c); float f = x_to_f(c, SR(0)); if (x_endm(c, o, X_IE | X_OE | X_UE)) return 0; memcpy((void*) ea, &f, 4); if (r == 3) xpop(c); return 0; }
            case 4: c->fcw = (u16) mrd(ea, 2); c->fsw = (u16)(mrd(ea + 4, 2) & 0x477f); c->top = (int)((mrd(ea + 4, 2) >> 11) & 7); x_settag(c, (u16) mrd(ea + 8, 2)); return 0;
            case 5: c->fcw = (u16) mrd(ea, 2); return 0;
            case 6: memset((void*) ea, 0, 28); mwr(ea, c->fcw, 2); mwr(ea + 4, x_sw(c), 2); mwr(ea + 8, x_tagword(c), 2); c->fcw |= 0x3f; return 0;   /* fnstenv masks every exception */
            case 7: mwr(ea, c->fcw, 2); return 0;
            }
            return -1;
        case 0xDD:
            switch (r) {
            case 0: { double d; memcpy(&d, (void*) ea, 8); xpush(c, x_from_d(c, d)); return 0; }
            case 2: case 3: { u16 o = x_begin(c); double d = x_to_d(c, SR(0)); if (x_endm(c, o, X_IE | X_OE | X_UE)) return 0; memcpy((void*) ea, &d, 8); if (r == 3) xpop(c); return 0; }
            case 7: mwr(ea, x_sw(c), 2); return 0;
            case 1: { u16 o = x_begin(c); u64 val = x_toint(c, SR(0), 8, 1); if (x_endm(c, o, X_IE)) return 0; mwr(ea, val, 8); xpop(c); return 0; }                     /* fisttp m64int (SSE3) */
            case 4: {                                                                          /* frstor: 28-byte environment, then ST(0..7) as 80-bit */
                u16 sw = (u16) mrd(ea + 4, 2);
                c->fcw = (u16) mrd(ea, 2); c->fsw = sw & 0x477f; c->top = (sw >> 11) & 7; x_settag(c, (u16) mrd(ea + 8, 2));
                for (int idx = 0; idx < 8; idx++) ST(idx) = ld_from80((u8*)(ea + 28 + 10 * (u64) idx));
                return 0; }
            case 6:                                                                            /* fnsave: environment + ST(0..7), then fninit */
                memset((void*) ea, 0, 28); mwr(ea, c->fcw, 2); mwr(ea + 4, x_sw(c), 2); mwr(ea + 8, x_tagword(c), 2);
                for (int idx = 0; idx < 8; idx++) ld_to80(ST(idx), (u8*)(ea + 28 + 10 * (u64) idx));
                c->fcw = 0x37f; c->fsw = 0; c->top = 0; c->ftw = 0;
                return 0;
            }
            return -1;
        case 0xDB:
            switch (r) {
            case 0: xpush(c, (long double)(s32) mrd(ea, 4)); return 0;
            case 2: case 3: { u16 o = x_begin(c); u64 val = x_toint(c, SR(0), 4, 0); if (x_endm(c, o, X_IE)) return 0; mwr(ea, val, 4); if (r == 3) xpop(c); return 0; }
            case 5: xpush(c, ld_from80((u8*) ea)); return 0;
            case 7: ld_to80(SR(0), (u8*) ea); xpop(c); return 0;
            case 1: { u16 o = x_begin(c); u64 val = x_toint(c, SR(0), 4, 1); if (x_endm(c, o, X_IE)) return 0; mwr(ea, val, 4); xpop(c); return 0; }                     /* fisttp m32int (SSE3) */
            }
            return -1;
        case 0xDF:
            switch (r) {
            case 0: xpush(c, (long double)(s16) mrd(ea, 2)); return 0;
            case 2: case 3: { u16 o = x_begin(c); u64 val = x_toint(c, SR(0), 2, 0); if (x_endm(c, o, X_IE)) return 0; mwr(ea, val, 2); if (r == 3) xpop(c); return 0; }
            case 5: xpush(c, (long double)(s64) mrd(ea, 8)); return 0;
            case 7: { u16 o = x_begin(c); u64 val = x_toint(c, SR(0), 8, 0); if (x_endm(c, o, X_IE)) return 0; mwr(ea, val, 8); xpop(c); return 0; }
            case 1: { u16 o = x_begin(c); u64 val = x_toint(c, SR(0), 2, 1); if (x_endm(c, o, X_IE)) return 0; mwr(ea, val, 2); xpop(c); return 0; }                     /* fisttp m16int (SSE3) */
            case 4: {                                                                          /* fbld: 18 packed BCD digits (low nibble first), sign in bit 7 of byte 9 */
                const u8*b = (const u8*) ea; long double val = 0;
                for (int idx = 8; idx >= 0; idx--) val = val * 100 + (b[idx] >> 4) * 10 + (b[idx] & 15);
                xpush(c, (b[9] & 0x80) ? -val : val); return 0; }
            case 6: {                                                                          /* fbstp */
                u8*b = (u8*) ea; long double sv = SR(0), val = isnan(sv) ? sv : xround(c, sv); int neg = signbit(val) != 0;
                if (neg) val = -val;
                if (isnan(val) || val >= 1e18L) { x_exc(c, X_IE); memset(b, 0, 10); b[7] = 0xC0; b[8] = 0xFF; b[9] = 0xFF; }   /* packed-BCD indefinite */
                else { if (val != fabsl(sv)) x_exc(c, X_PE); u64 n = (u64) val; for (int idx = 0; idx < 9; idx++) { b[idx] = (u8)((n % 10) | ((n / 10 % 10) << 4)); n /= 100; } b[9] = neg ? 0x80 : 0; }
                xpop(c); return 0; }
            }
            return -1;
        }
        return -1;
    }
    switch (op) {
    case 0xD8: { u16 o = x_begin(c);
        if (r == 2 || r == 3) { x_cc(c, SR(0), SR(i), 1); if (x_end(c, o)) return 0; if (r == 3) xpop(c); }
        else { long double q = xop(c, r, SR(0), SR(i)); if (x_end(c, o)) return 0; ST(0) = q; }
        return 0; }
    case 0xDC: case 0xDE: {
        if (op == 0xDE && r == 3 && i == 1) { u16 o = x_begin(c); x_cc(c, SR(0), SR(1), 1); if (x_end(c, o)) return 0; xpop(c); xpop(c); return 0; }       /* fcompp */
        if (r == 2 || r == 3) return -1;
        int rr = r >= 4 ? (r ^ 1) : r;
        u16 o = x_begin(c); long double q = xop(c, rr, SR(i), SR(0));
        if (x_end(c, o)) return 0;
        ST(i) = q;
        if (op == 0xDE) xpop(c);
        return 0; }
    case 0xD9:
        switch (r) {
        case 0: xpush(c, SR(i)); return 0;
        case 1: { long double t = ST(0); ST(0) = ST(i); ST(i) = t; return 0; }
        case 2: return i == 0 ? 0 : -1;
        case 4:
            switch (i) { case 0: ST(0) = -SR(0); return 0; case 1: ST(0) = fabsl(SR(0)); return 0; case 4: x_cc(c, SR(0), 0.0L, 1); return 0;
            case 5: { long double v = ST(0); c->fsw &= ~0x4700; if (signbit(v)) c->fsw |= 0x200;
                      if (!((c->ftw >> c->top) & 1)) { c->fsw |= 0x4100; return 0; }                                  /* empty */
                      switch (fpclassify(v)) { case FP_NAN: c->fsw |= 0x100; break; case FP_INFINITE: c->fsw |= 0x500; break; case FP_ZERO: c->fsw |= 0x4000; break; case FP_SUBNORMAL: c->fsw |= 0x4400; break; default: c->fsw |= 0x400; } return 0; } }
            return -1;
        case 5:
            switch (i) { case 0: xpush(c, 1.0L); return 0; case 1: xpush(c, 3.321928094887362347870319429489390175864831393L); return 0;
            case 2: xpush(c, 1.442695040888963407359924681001892137L); return 0; case 3: xpush(c, 3.141592653589793238462643383279502884L); return 0;
            case 4: xpush(c, 0.301029995663981195213738894724493026768189881L); return 0; case 5: xpush(c, 0.693147180559945309417232121458176568L); return 0;
            case 6: xpush(c, 0.0L); return 0; }
            return -1;
        case 6:
            switch (i) {
            case 0: { long double v = SR(0);                                                                         /* f2xm1 */
                if (isnan(v)) { if (x_issnan(v)) x_exc(c, X_IE); ST(0) = x_quiet(v); return 0; }
                if (x_den(v)) x_exc(c, X_DE);
                if (v == 0) return 0;
                if (isinf(v)) { ST(0) = v > 0 ? v : -1.0L; return 0; }
                ST(0) = fabsl(v) <= 8 ? x_fin(c, q_exp2m1(v)) : expm1l(v * 0.693147180559945309417232121458176568L); return 0; }
            case 1: { long double x = SR(0), y = SR(1); xpop(c); ST(0) = x_yl2x(c, x, y, 0); return 0; }
            case 2: { long double o1, o2; int tr = x_trig(c, SR(0), 2, &o1, &o2); if (!tr) return 0; ST(0) = o1; xpush(c, tr == 1 ? 1.0L : o2); return 0; }    /* fptan */
            case 3: { long double x = SR(0), y = SR(1); xpop(c);                                                      /* fpatan */
                if (isnan(x) || isnan(y)) { if (x_issnan(x) || x_issnan(y)) x_exc(c, X_IE); ST(0) = x_nanpick(x, y); return 0; }
                if (x_den(x) || x_den(y)) x_exc(c, X_DE);
                ST(0) = x_patan(c, y, x); return 0; }
            case 4: { long double v = SR(0); int e; if (v == 0) { x_exc(c, X_ZE); ST(0) = -INFINITY; xpush(c, v); return 0; }                    /* fxtract */
                if (isnan(v)) { if (x_issnan(v)) x_exc(c, X_IE); ST(0) = x_quiet(v); xpush(c, ST(0)); return 0; }
                if (isinf(v)) { ST(0) = INFINITY; xpush(c, v); return 0; }
                if (x_den(v)) x_exc(c, X_DE);
                long double f = frexpl(v, &e); ST(0) = (long double)(e - 1); xpush(c, f * 2); return 0; }
            case 5: { long double a = SR(0), b = SR(1); ST(0) = x_prem(c, a, b, 0); return 0; }
            case 6: c->top = (c->top - 1) & 7; return 0;
            case 7: c->top = (c->top + 1) & 7; return 0;
            }
            return -1;
        case 7:
            switch (i) {
            case 0: { long double a = SR(0), b = SR(1); ST(0) = x_prem(c, a, b, 1); return 0; }
            case 1: { long double x = SR(0), y = SR(1); xpop(c); ST(0) = x_yl2x(c, x, y, 1); return 0; }
            case 2: { u16 o = x_begin(c); long double q = x_sqrt(c, SR(0)); if (x_end(c, o)) return 0; ST(0) = q; return 0; }
            case 3: { long double o1, o2; if (!x_trig(c, SR(0), 3, &o1, &o2)) return 0; ST(0) = o1; xpush(c, o2); return 0; }
            case 4: ST(0) = x_rint(c, SR(0)); return 0;
            case 5: { long double a = SR(0), b = SR(1); ST(0) = x_scale(c, a, b); return 0; }
            case 6: { long double o1, o2; if (x_trig(c, SR(0), 0, &o1, &o2)) ST(0) = o1; return 0; }
            case 7: { long double o1, o2; if (x_trig(c, SR(0), 1, &o1, &o2)) ST(0) = o1; return 0; }
            }
            return -1;
        }
        return -1;
    case 0xDA:
        if (r == 5 && i == 1) { x_cc(c, SR(0), SR(1), 0); xpop(c); xpop(c); return 0; }                 /* fucompp */
        if (r < 4) { int cc = r == 0 ? (RF(c) & CF_) : r == 1 ? (RF(c) & ZF_) : r == 2 ? (RF(c) & (CF_ | ZF_)) : (RF(c) & PF_); if (cc) ST(0) = ST(i); return 0; }
        return -1;
    case 0xDB:
        if (r < 4) { int cc = r == 0 ? (RF(c) & CF_) : r == 1 ? (RF(c) & ZF_) : r == 2 ? (RF(c) & (CF_ | ZF_)) : (RF(c) & PF_); if (!cc) ST(0) = ST(i); return 0; }
        if (r == 4 && i == 2) { c->fsw &= (u16) ~0x7f; return 0; }                                      /* fnclex: exception flags and SF; the condition codes stay */
        if (r == 4 && i == 3) { c->fcw = 0x37f; c->fsw = 0; c->top = 0; c->ftw = 0; return 0; }
        if (r == 4 && (i == 0 || i == 1 || i == 4)) return 0;                                         /* fneni, fndisi, fnsetpm: 8087/287 leftovers, nops */
        if (r == 5 || r == 6) { x_flags(c, SR(0), SR(i), r == 6); return 0; }
        return -1;
    case 0xDD:
        if (r == 0) { c->ftw &= (u8) ~(1u << ((c->top + i) & 7)); return 0; }                         /* ffree */
        if (r == 2 || r == 3) { ST(i) = SR(0); if (r == 3) xpop(c); return 0; }
        if (r == 4 || r == 5) { x_cc(c, SR(0), SR(i), 0); if (r == 5) xpop(c); return 0; }
        return -1;
    case 0xDF:
        if (r == 0) { c->ftw &= (u8) ~(1u << ((c->top + i) & 7)); xpop(c); return 0; }              /* ffreep: free ST(i), then pop */
        if (r == 2 || r == 3) { ST(i) = SR(0); if (r == 3) xpop(c); return 0; }                         /* undocumented fst/fstp alias, as DD D0 / DD D8 */
        if (r == 4 && i == 0) { setr(c, RAX, 2, 0, x_sw(c)); return 0; }
        if (r == 5 || r == 6) { x_flags(c, SR(0), SR(i), r == 6); xpop(c); return 0; }
        return -1;
    }
    return -1;
}


#!/usr/bin/env python3
"""Exact oracle for host/rt/rt_quad.h in every rounding mode: reads the vectors of `rt_quad_test -d` (op mode a b c result, binary128 as 32 hex digits)
and recomputes fma / sqrt / ldexp with Fractions."""
import sys
from fractions import Fraction
from math import isqrt
def dec(h):
    b = int(h, 16); s = b >> 127; e = (b >> 112) & 0x7fff; f = b & (2**112 - 1)
    if e == 0x7fff: return ('nan' if f else 'inf', s)
    m = f | (1 << 112) if e else f
    return ('f', s, Fraction(m) * Fraction(2) ** ((e or 1) - 16495))
def enc(sign, v, mode):          # v >= 0 Fraction, round to binary128
    S = sign << 127
    if v == 0: return S
    e = v.numerator.bit_length() - v.denominator.bit_length()
    if Fraction(2) ** e > v: e -= 1
    qt = max(e - 112, -16494); u = v / Fraction(2) ** qt; Q = u.numerator // u.denominator; r = u - Q
    up = False
    if r:
        if mode == 0: up = r > Fraction(1, 2) or (r == Fraction(1, 2) and Q & 1)
        elif mode == 1: up = bool(sign)
        elif mode == 2: up = not sign
    Q += up
    if Q >> 113: Q >>= 1; qt += 1
    if Q == 0: return S
    ex = qt + 16495 if Q >> 112 else 0
    if ex >= 0x7fff:
        if mode == 0 or (mode == 1 and sign) or (mode == 2 and not sign): return S | (0x7fff << 112)
        return S | (0x7ffe << 112) | (2**112 - 1)
    return S | (ex << 112) | (Q & (2**112 - 1))
def val(d): return d[2] * (-1 if d[1] else 1)
def fin(x, mode):                # exact signed Fraction -> bits; exact zero: +0 (or -0 rounding down)
    if x == 0: return (1 << 127) if mode == 1 else 0
    return enc(1 if x < 0 else 0, abs(x), mode)
bad = n = 0
for line in sys.stdin:
    op, mode, a, b, c, r = line.split(); mode = int(mode); A, B, C = dec(a), dec(b), dec(c); got = int(r, 16); n += 1
    if op == 'fma':
        if 'nan' in (A[0], B[0], C[0]) or A[0] == 'inf' or B[0] == 'inf' or C[0] == 'inf': continue
        p = val(A) * val(B); x = p + val(C)
        if p == 0 and val(C) == 0:
            sg = (A[1] ^ B[1]); want = (sg << 127) if sg == C[1] else ((1 << 127) if mode == 1 else 0)
        else: want = fin(x, mode)
    elif op == 'sqrt':
        if A[0] != 'f' or (A[1] and A[2] != 0): continue
        v = val(A)
        if v == 0: want = int(a, 16)
        else:                     # sqrt(v) between R and R+1 in units 2^-k, k large enough for 113 bits + guard
            k = 8400; N = v * 2 ** (2 * k); R = isqrt(N.numerator // N.denominator); exact = R * R == N
            # build a value strictly between when inexact: use R + 1/2 (never a rounding boundary at this scale)
            want = enc(0, Fraction(R if exact else 2 * R + 1, 1 if exact else 2) / 2 ** k, mode)
    elif op == 'ldexp':
        if A[0] != 'f' or A[2] == 0: continue
        n_ = int(b[-8:], 16); n_ = n_ - 2**32 if n_ >= 2**31 else n_
        want = enc(A[1], A[2] * Fraction(2) ** n_, mode)
    else: continue
    if want != got:
        bad += 1
        if bad < 10: print('MISMATCH', op, mode, a, b, c, '%032x' % got, '%032x' % want)
print(f'oracle: {n} vectors, {bad} mismatches'); sys.exit(1 if bad else 0)

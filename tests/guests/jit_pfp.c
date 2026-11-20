/* tests/guests/jit_pfp.c - packed float arithmetic: addps subps mulps divps sqrtps and the pd forms, register and memory source, unpcklps/unpckhps/unpcklpd/unpckhpd
 * (guest for tests/integration/jit_embed.sh). Inputs are finite and sqrt/div operands are positive: the sign of a default NaN differs between x86 and AArch64.
 * Prints a hash per round; interpreter, JIT and native must agree. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <emmintrin.h>
typedef uint64_t u64;
static u64 h;
static void mix(const void *p, int n) { const unsigned char *q = p; for (int i = 0; i < n; i++) { h ^= q[i]; h *= 1099511628211UL; } }
static unsigned long long rng = 88172645463325252ULL;
static u64 rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static __attribute__((aligned(16))) float FA[4], FB[4];
static __attribute__((aligned(16))) double DA[2], DB[2];
#define RR(name, ins, T, V) static __attribute__((noinline)) V name(V a, V b) { __asm__(ins " %1, %0" : "+x"(a) : "x"(b)); return a; }
#define RM(name, ins, T, V, MEM) static __attribute__((noinline)) V name(V a) { __asm__(ins " (%1), %0" : "+x"(a) : "r"(MEM), "m"(*(const char (*)[16]) MEM)); return a; }
RR(addps_rr, "addps", float, __m128) RR(subps_rr, "subps", float, __m128) RR(mulps_rr, "mulps", float, __m128) RR(divps_rr, "divps", float, __m128)
RR(addpd_rr, "addpd", double, __m128d) RR(subpd_rr, "subpd", double, __m128d) RR(mulpd_rr, "mulpd", double, __m128d) RR(divpd_rr, "divpd", double, __m128d)
RR(unpcklps_rr, "unpcklps", float, __m128) RR(unpckhps_rr, "unpckhps", float, __m128) RR(unpcklpd_rr, "unpcklpd", double, __m128d) RR(unpckhpd_rr, "unpckhpd", double, __m128d)
RM(addps_rm, "addps", float, __m128, FB) RM(subps_rm, "subps", float, __m128, FB) RM(mulps_rm, "mulps", float, __m128, FB) RM(divps_rm, "divps", float, __m128, FB)
RM(addpd_rm, "addpd", double, __m128d, DB) RM(mulpd_rm, "mulpd", double, __m128d, DB) RM(divpd_rm, "divpd", double, __m128d, DB)
RM(sqrtps_rm, "sqrtps", float, __m128, FB) RM(sqrtpd_rm, "sqrtpd", double, __m128d, DB)
RM(unpcklps_rm, "unpcklps", float, __m128, FB) RM(unpckhpd_rm, "unpckhpd", double, __m128d, DB)
static __attribute__((noinline)) __m128 sqrtps_rr(__m128 a, __m128 b) { __asm__("sqrtps %1, %0" : "+x"(a) : "x"(b)); return a; }
static __attribute__((noinline)) __m128d sqrtpd_rr(__m128d a, __m128d b) { __asm__("sqrtpd %1, %0" : "+x"(a) : "x"(b)); return a; }
static __attribute__((noinline)) float saxpy(float *y, const float *x, float k, int n) {      /* the loop a compiler vectorises */
    __m128 kk = _mm_set1_ps(k), acc = _mm_setzero_ps();
    for (int i = 0; i < n; i += 4) { __m128 v = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(x + i), kk), _mm_loadu_ps(y + i)); _mm_storeu_ps(y + i, v); acc = _mm_add_ps(acc, v); }
    float t[4]; _mm_storeu_ps(t, acc); return t[0] + t[1] + t[2] + t[3];
}
int main(void) {
    static float X[64], Y[64];
    for (int it = 0; it < 300; it++) {
        for (int i = 0; i < 4; i++) {
            FA[i] = (float)((int)(rnd() % 2000001) - 1000000) / 1024.0f * (it % 5 == 4 ? 1e-30f : 1.0f);
            FB[i] = (float)(rnd() % 1000000 + 1) / 512.0f;                       /* positive, non-zero */
        }
        for (int i = 0; i < 2; i++) { DA[i] = (double)((long)(rnd() % 20000001) - 10000000) / 4096.0; DB[i] = (double)(rnd() % 10000000 + 1) / 2048.0; }
        __m128 a = _mm_load_ps(FA), b = _mm_load_ps(FB); __m128d da = _mm_load_pd(DA), db = _mm_load_pd(DB);
        __m128 r;
        r = addps_rr(a, b); mix(&r, 16); r = subps_rr(a, b); mix(&r, 16); r = mulps_rr(a, b); mix(&r, 16); r = divps_rr(a, b); mix(&r, 16);
        r = sqrtps_rr(a, b); mix(&r, 16); r = sqrtps_rm(a); mix(&r, 16);
        r = addps_rm(a); mix(&r, 16); r = subps_rm(a); mix(&r, 16); r = mulps_rm(a); mix(&r, 16); r = divps_rm(a); mix(&r, 16);
        r = unpcklps_rr(a, b); mix(&r, 16); r = unpckhps_rr(a, b); mix(&r, 16); r = unpcklps_rm(a); mix(&r, 16);
        __m128d d;
        d = addpd_rr(da, db); mix(&d, 16); d = subpd_rr(da, db); mix(&d, 16); d = mulpd_rr(da, db); mix(&d, 16); d = divpd_rr(da, db); mix(&d, 16);
        d = sqrtpd_rr(da, db); mix(&d, 16); d = sqrtpd_rm(da); mix(&d, 16);
        d = addpd_rm(da); mix(&d, 16); d = mulpd_rm(da); mix(&d, 16); d = divpd_rm(da); mix(&d, 16);
        d = unpcklpd_rr(da, db); mix(&d, 16); d = unpckhpd_rr(da, db); mix(&d, 16); d = unpckhpd_rm(da); mix(&d, 16);
        if (it % 20 == 0) {
            for (int i = 0; i < 64; i++) { X[i] = (float)(int)(rnd() % 2001 - 1000) / 8.0f; Y[i] = (float)(int)(rnd() % 2001 - 1000) / 16.0f; }
            float s = saxpy(Y, X, 0.75f, 64); mix(&s, 4); mix(Y, 256);
        }
        if (it % 50 == 49) printf("round %d %016lx\n", it, h);
    }
    return 0;
}

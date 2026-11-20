/* tests/guests/jit_shuf.c - shufps / shufpd (all 256 / 4 immediates, register and memory source) and cvtdq2ps (guest for tests/integration/jit_embed.sh).
 * Prints a hash per case; interpreter, JIT and native must agree. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <emmintrin.h>
typedef uint64_t u64;
static u64 h;
static void mix(const void *p, int n) { const unsigned char *q = p; for (int i = 0; i < n; i++) { h ^= q[i]; h *= 1099511628211UL; } }
static unsigned long long rng = 88172645463325252ULL;
static u64 rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static __attribute__((aligned(16))) unsigned char B[16];
#define SP(n) case n: r = _mm_shuffle_ps(a, b, n); break;
#define SP4(n) SP(n) SP(n + 1) SP(n + 2) SP(n + 3)
#define SP16(n) SP4(n) SP4(n + 4) SP4(n + 8) SP4(n + 12)
static __attribute__((noinline)) __m128 shr(__m128 a, __m128 b, int n) { __m128 r; switch (n) { SP16(0) SP16(16) SP16(32) SP16(48) SP16(64) SP16(80) SP16(96) SP16(112) SP16(128) SP16(144) SP16(160) SP16(176) SP16(192) SP16(208) SP16(224) SP16(240) default: r = a; } return r; }
#define SM(n) case n: r = _mm_shuffle_ps(a, *(volatile __m128 *) B, n); break;
#define SM4(n) SM(n) SM(n + 1) SM(n + 2) SM(n + 3)
#define SM16(n) SM4(n) SM4(n + 4) SM4(n + 8) SM4(n + 12)
static __attribute__((noinline)) __m128 shm(__m128 a, int n) { __m128 r; switch (n) { SM16(0) SM16(16) SM16(32) SM16(48) SM16(64) SM16(80) SM16(96) SM16(112) SM16(128) SM16(144) SM16(160) SM16(176) SM16(192) SM16(208) SM16(224) SM16(240) default: r = a; } return r; }
#define PD(n) case n: r = _mm_shuffle_pd(a, b, n); break;
static __attribute__((noinline)) __m128d pdr(__m128d a, __m128d b, int n) { __m128d r; switch (n) { PD(0) PD(1) PD(2) PD(3) default: r = a; } return r; }
#define PM(n) case n: r = _mm_shuffle_pd(a, *(volatile __m128d *) B, n); break;
static __attribute__((noinline)) __m128d pdm(__m128d a, int n) { __m128d r; switch (n) { PM(0) PM(1) PM(2) PM(3) default: r = a; } return r; }
static __attribute__((noinline)) __m128 cvr(__m128i a) { __m128 r; __asm__("cvtdq2ps %1, %0" : "=x"(r) : "x"(a)); return r; }
static __attribute__((noinline)) __m128 cvm(void) { __m128 r; __asm__ volatile("cvtdq2ps (%1), %0" : "=x"(r) : "r"(B) : "memory"); return r; }
int main(void) {
    __attribute__((aligned(16))) unsigned char A[16], C[16];
    h = 1469598103934665603ULL;
    for (int n = 0; n < 256; n++) { for (int i = 0; i < 16; i++) { A[i] = (unsigned char) rnd(); C[i] = (unsigned char) rnd(); B[i] = (unsigned char) rnd(); }
        __m128 r = shr(_mm_loadu_ps((float *) A), _mm_loadu_ps((float *) C), n); mix(&r, 16); r = shm(_mm_loadu_ps((float *) A), n); mix(&r, 16); }
    printf("shufps %016llx\n", (unsigned long long) h);
    h = 1469598103934665603ULL;
    for (int k = 0; k < 64; k++) { for (int i = 0; i < 16; i++) { A[i] = (unsigned char) rnd(); C[i] = (unsigned char) rnd(); B[i] = (unsigned char) rnd(); }
        for (int n = 0; n < 4; n++) { __m128d r = pdr(_mm_loadu_pd((double *) A), _mm_loadu_pd((double *) C), n); mix(&r, 16); r = pdm(_mm_loadu_pd((double *) A), n); mix(&r, 16); } }
    printf("shufpd %016llx\n", (unsigned long long) h);
    h = 1469598103934665603ULL;
    static const int sp[] = { 0, 1, -1, 16777216, 16777217, 33554435, 2147483647, (int) 0x80000000, 123456789, -987654321, 0x7fffff80, 0x7fffffc0 };
    for (int k = 0; k < 400; k++) { int v[4]; for (int i = 0; i < 4; i++) v[i] = k < 12 ? sp[(k + i) % 12] : (int) rnd();
        __m128 r = cvr(_mm_loadu_si128((__m128i *) v)); mix(&r, 16); memcpy(B, v, 16); r = cvm(); mix(&r, 16); }
    printf("cvtdq2ps %016llx\n", (unsigned long long) h);
    return 0;
}

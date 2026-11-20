/* tests/guests/jit_pmul.c - pmuludq on xmm and mm registers, register and memory source, edge values and random ones (guest for tests/integration/jit_embed.sh).
 * Prints a hash per round; interpreter, JIT and native must agree. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <emmintrin.h>
#include <mmintrin.h>
typedef uint64_t u64;
static u64 h;
static void mix(const void *p, int n) { const unsigned char *q = p; for (int i = 0; i < n; i++) { h ^= q[i]; h *= 1099511628211UL; } }
static unsigned long long rng = 88172645463325252ULL;
static u64 rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static __attribute__((aligned(16))) unsigned char A[16], B[16];
static __attribute__((noinline)) __m128i mul_rr(__m128i a, __m128i b) { __asm__("pmuludq %1, %0" : "+x"(a) : "x"(b)); return a; }
static __attribute__((noinline)) __m128i mul_rm(__m128i a) { __asm__("pmuludq (%1), %0" : "+x"(a) : "r"(B), "m"(*(const char (*)[16]) B)); return a; }
static __attribute__((noinline)) u64 mmx_rr(u64 a, u64 b) {
    u64 r; __asm__ volatile("movq %1, %%mm0\n\tmovq %2, %%mm1\n\tpmuludq %%mm1, %%mm0\n\tmovq %%mm0, %0\n\temms" : "=m"(r) : "m"(a), "m"(b) : "mm0", "mm1", "memory"); return r;
}
static __attribute__((noinline)) u64 mmx_rm(u64 a, const u64 *b) {
    u64 r; __asm__ volatile("movq %1, %%mm0\n\tpmuludq (%2), %%mm0\n\tmovq %%mm0, %0\n\temms" : "=m"(r) : "m"(a), "r"(b), "m"(*b) : "mm0", "memory"); return r;
}
static __attribute__((noinline)) __m128i mul_loop(const uint32_t *x, const uint32_t *y, int n) {   /* the pattern gcc emits for a vectorised 32-bit multiply */
    __m128i acc = _mm_setzero_si128();
    for (int i = 0; i < n; i += 4) {
        __m128i a = _mm_loadu_si128((const __m128i *)(x + i)), b = _mm_loadu_si128((const __m128i *)(y + i));
        __m128i e = _mm_mul_epu32(a, b), o = _mm_mul_epu32(_mm_srli_epi64(a, 32), _mm_srli_epi64(b, 32));
        acc = _mm_add_epi32(acc, _mm_unpacklo_epi32(_mm_shuffle_epi32(e, 8), _mm_shuffle_epi32(o, 8)));
    }
    return acc;
}
int main(void) {
    static const u64 edge[] = { 0, 1, 0xffffffffULL, 0x100000000ULL, 0xffffffffffffffffULL, 0x80000000ULL, 0x7fffffffULL, 0xffffffff00000000ULL, 0x00000001ffffffffULL };
    static uint32_t X[64], Y[64];
    for (int it = 0; it < 300; it++) {
        for (int i = 0; i < 16; i++) { A[i] = (unsigned char) rnd(); B[i] = (unsigned char) rnd(); }
        if (it < 81) { u64 e0 = edge[it % 9], e1 = edge[(it / 9) % 9]; memcpy(A, &e0, 8); memcpy(A + 8, &e1, 8); memcpy(B, &e1, 8); memcpy(B + 8, &e0, 8); }
        __m128i a = _mm_load_si128((__m128i *) A), b = _mm_load_si128((__m128i *) B);
        __m128i r = mul_rr(a, b); mix(&r, 16);
        r = mul_rm(a); mix(&r, 16);
        u64 a0, b0; memcpy(&a0, A, 8); memcpy(&b0, B, 8);
        u64 m = mmx_rr(a0, b0); mix(&m, 8);
        m = mmx_rm(a0, (const u64 *) B); mix(&m, 8);
        if (it % 20 == 0) {
            for (int i = 0; i < 64; i++) { X[i] = (uint32_t) rnd(); Y[i] = (uint32_t) rnd(); }
            r = mul_loop(X, Y, 64); mix(&r, 16);
        }
        if (it % 50 == 49) { printf("round %d %016lx\n", it, h); }
    }
    return 0;
}

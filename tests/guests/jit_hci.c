/* JIT test: SSE / SSE3 / SSSE3 / SSE4.2 / x87 / fxsave instructions translated as direct class-entry calls (JO_HCI). native vs interpreter vs JIT must agree. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <immintrin.h>
typedef unsigned long long u64;
static volatile unsigned char buf[3][32] __attribute__((aligned(16)));
static void seed(int k) { for (int i = 0; i < 32; i++) { buf[0][i] = (unsigned char)(i * 37 + k * 11 + 5); buf[1][i] = (unsigned char)(i * 91 + k * 7 + 200); buf[2][i] = (unsigned char)((i ^ k) * 53 + 1); }
    if (k & 1) { buf[0][3] = 0x80; buf[0][4] = 0x7f; buf[1][8] = 0xff; buf[1][9] = 0xff; } if (k & 2) memset((void*) buf[1], 0x41, 16); }
static void pr(const char *n, __m128i v) { unsigned long long q[2]; memcpy(q, &v, 16); printf("%s %016llx %016llx\n", n, q[0], q[1]); }
static void prf(const char *n, __m128 v) { unsigned q[4]; memcpy(q, &v, 16); printf("%s %08x %08x %08x %08x\n", n, q[0], q[1], q[2], q[3]); }
static void prd(const char *n, __m128d v) { unsigned long long q[2]; memcpy(q, &v, 16); printf("%s %016llx %016llx\n", n, q[0], q[1]); }
#define A (_mm_load_si128((const __m128i*) buf[0]))
#define B (_mm_load_si128((const __m128i*) buf[1]))
static __m128 F(int i, int k) { float f[4] = { 1.5f * i - 3, -2.25f + k, 1e10f * (k & 1), (i & 1) ? NAN : 0.5f }; if (k == 3) f[1] = INFINITY; return _mm_loadu_ps(f); }
static __m128d D(int i, int k) { double d[2] = { 1.5 * i - 3.25, (k & 1) ? NAN : -7.5 + k }; if (k == 2) d[0] = INFINITY; return _mm_loadu_pd(d); }
int main(void) {
    for (int k = 0; k < 4; k++) {
        seed(k);
        pr("pshuflw", _mm_shufflelo_epi16(A, 0x1b)); pr("pshufhw", _mm_shufflehi_epi16(B, 0xc6));
        pr("packsswb", _mm_packs_epi16(A, B)); pr("packssdw", _mm_packs_epi32(A, B));
        pr("pmulhuw", _mm_mulhi_epu16(A, B)); pr("pmulhw", _mm_mulhi_epi16(A, B));
        pr("pmaddwd", _mm_madd_epi16(A, B)); pr("psadbw", _mm_sad_epu8(A, B));
        pr("psrld-x", _mm_srl_epi32(A, B)); pr("pslld-x", _mm_sll_epi32(A, B)); pr("psrad-x", _mm_sra_epi32(A, B)); pr("psrlw-x", _mm_srl_epi16(A, B));
        pr("psllq-x", _mm_sll_epi64(A, B)); pr("psraw-x", _mm_sra_epi16(A, B)); pr("psrlq-x", _mm_srl_epi64(A, A));
        pr("pinsrw", _mm_insert_epi16(A, 0x1234 + k, 5)); printf("pextrw %x\n", _mm_extract_epi16(B, 3));
        pr("pinsrq", _mm_insert_epi64(A, 0x1122334455667788ULL + k, 1)); printf("pextrq %llx\n", (u64) _mm_extract_epi64(B, 1));
        pr("pinsrd", _mm_insert_epi32(A, 0x99aabbcc + k, 2)); printf("pextrd %x\n", _mm_extract_epi32(B, 0)); printf("pextrb %x\n", _mm_extract_epi8(B, 7));
        pr("pinsrb", _mm_insert_epi8(A, 0x5a, 9)); pr("blendw", _mm_blend_epi16(A, B, 0x5a)); pr("pblendvb", _mm_blendv_epi8(A, B, _mm_cmplt_epi8(A, B)));
        printf("pcmpistri %d %d %d\n", _mm_cmpistri(A, B, 0x0c), _mm_cmpistri(A, B, 0x18), _mm_cmpistri(B, B, 0x00));
        printf("pcmpistri-f %d %d %d\n", _mm_cmpistrc(A, B, 0x0c), _mm_cmpistrz(A, B, 0x0c), _mm_cmpistro(A, B, 0x0c));
        printf("pcmpestri %d\n", _mm_cmpestri(A, 11, B, 13, 0x0c)); pr("pcmpistrm", _mm_cmpistrm(A, B, 0x40));
        { unsigned crc = 0x12345678; crc = _mm_crc32_u8(crc, 0xab); crc = _mm_crc32_u32(crc, 0xdeadbeef + k); crc = (unsigned) _mm_crc32_u64(crc, 0x0123456789abcdefULL); printf("crc32 %x\n", crc); }
        pr("phaddw", _mm_hadd_epi16(A, B)); pr("phaddd", _mm_hadd_epi32(A, B)); pr("phsubw", _mm_hsub_epi16(A, B)); pr("pmaddubsw", _mm_maddubs_epi16(A, B));
        pr("psignb", _mm_sign_epi8(A, B)); pr("pmulhrsw", _mm_mulhrs_epi16(A, B)); pr("palignr3a", _mm_alignr_epi8(A, B, 5));
        pr("pmovsxbw", _mm_cvtepi8_epi16(A)); pr("pmaxsb", _mm_max_epi8(A, B)); pr("pminud", _mm_min_epu32(A, B)); pr("mpsadbw", _mm_mpsadbw_epu8(A, B, 5)); pr("phminposuw", _mm_minpos_epu16(A));
        pr("roundps-i", _mm_castps_si128(_mm_round_ps(F(1, k), 1))); pr("roundsd", _mm_castpd_si128(_mm_round_sd(D(2, k), D(3, k), 2)));
        pr("dpps", _mm_castps_si128(_mm_dp_ps(F(2, k), F(3, k), 0xf1))); pr("insertps", _mm_castps_si128(_mm_insert_ps(F(2, k), F(1, k), 0x10)));
        for (int i = 0; i < 4; i++) {
            __m128 f = F(i, k), g = F(3 - i, k + 1); __m128d d = D(i, k), e = D(3 - i, k + 1);
            prf("shufps", _mm_shuffle_ps(f, g, 0x4e)); prd("shufpd", _mm_shuffle_pd(d, e, 1));
            prf("cmpeqps", _mm_cmpeq_ps(f, g)); prf("cmpltps", _mm_cmplt_ps(f, g)); prf("cmpunordps", _mm_cmpunord_ps(f, g)); prf("cmpnleps", _mm_cmpnle_ps(f, g));
            prf("cmpless", _mm_cmple_ss(f, g)); prd("cmpneqsd", _mm_cmpneq_sd(d, e)); prd("cmpltpd", _mm_cmplt_pd(d, e)); prd("cmpordsd", _mm_cmpord_sd(d, e));
            prf("minps", _mm_min_ps(f, g)); prf("maxps", _mm_max_ps(f, g)); prd("minpd", _mm_min_pd(d, e)); prd("maxpd", _mm_max_pd(d, e));
            prf("haddps", _mm_hadd_ps(f, g)); prd("haddpd", _mm_hadd_pd(d, e)); prf("hsubps", _mm_hsub_ps(f, g)); prd("hsubpd", _mm_hsub_pd(d, e));
            prf("addsubps", _mm_addsub_ps(f, g)); prd("addsubpd", _mm_addsub_pd(d, e));
            printf("movmskps %d movmskpd %d\n", _mm_movemask_ps(f), _mm_movemask_pd(d));
            prd("cvtdq2pd", _mm_cvtepi32_pd(_mm_castps_si128(f))); pr("cvtpd2dq", _mm_cvtpd_epi32(d)); pr("cvttpd2dq", _mm_cvttpd_epi32(d));
            prf("cvtdq2ps", _mm_cvtepi32_ps(_mm_castps_si128(g))); pr("cvtps2dq", _mm_cvtps_epi32(f)); pr("cvttps2dq", _mm_cvttps_epi32(g));
            prd("cvtps2pd", _mm_cvtps_pd(f)); prf("cvtpd2ps", _mm_cvtpd_ps(d));
            printf("cvtsd2si %d %lld cvtss2si %d %lld\n", _mm_cvtsd_si32(d), (long long) _mm_cvtsd_si64(d), _mm_cvtss_si32(f), (long long) _mm_cvtss_si64(g));
            prf("movshdup", _mm_movehdup_ps(f)); prf("movsldup", _mm_moveldup_ps(g)); prd("movddup", _mm_movedup_pd(d));
            pr("lddqu", _mm_lddqu_si128((const __m128i*)(buf[2] + (k & 1))));
        }
    }
    /* x87 */
    volatile double x = 1.25, y = -3.5; volatile float fx = 2.75f; volatile long long li = 123456789012LL; volatile short si = -42;
    for (int k = 0; k < 4; k++) {
        double r;
        __asm__ volatile("fldl %1\n fmull %2\n faddl %1\n fsqrt\n fstpl %0" : "=m"(r) : "m"(x), "m"(y) : "st"); printf("x87a %.17g\n", r);
        __asm__ volatile("fldl %1\n fldl %2\n fxch\n fdivrp\n fstpl %0" : "=m"(r) : "m"(x), "m"(y) : "st", "st(1)"); printf("x87b %.17g\n", r);
        __asm__ volatile("fildll %1\n fadds %2\n fstpl %0" : "=m"(r) : "m"(li), "m"(fx) : "st"); printf("x87c %.17g\n", r);
        long long o; __asm__ volatile("fldl %1\n fistpll %0" : "=m"(o) : "m"(y) : "st"); printf("x87d %lld\n", o);
        short sh; __asm__ volatile("fldl %1\n fistps %0" : "=m"(sh) : "m"(x) : "st"); printf("x87e %d\n", sh);
        __asm__ volatile("filds %1\n fchs\n fabs\n fstpl %0" : "=m"(r) : "m"(si) : "st"); printf("x87f %.17g\n", r);
        unsigned short cw, sw; __asm__ volatile("fnstcw %0" : "=m"(cw)); printf("x87cw %x\n", cw);
        __asm__ volatile("fldl %2\n fcomip %%st(1), %%st\n fstp %%st(0)\n fnstsw %0" : "=m"(sw) : "m"(y), "m"(x) : "st", "st(1)", "cc"); printf("~x87sw\n");
        double t; __asm__ volatile("fldl %1\n fldl %2\n fcomi %%st(1), %%st\n fstp %%st(0)\n fstp %%st(0)\n setb %%al\n movzbl %%al,%%eax\n" : "=a"(t) : "m"(x), "m"(y) : "st", "st(1)", "cc"); (void) t;
        long double ld; __asm__ volatile("fldl %1\n fldl %1\n fmulp\n fldln2\n fyl2x\n fstpt %0" : "=m"(ld) : "m"(x) : "st", "st(1)"); printf("x87g %.15Lg\n", ld);
        __asm__ volatile("fld1\n fldl %1\n fscale\n fstp %%st(1)\n fstpl %0" : "=m"(r) : "m"(y) : "st", "st(1)"); printf("x87h %.17g\n", r);
        __asm__ volatile("fldl %1\n frndint\n fstpl %0" : "=m"(r) : "m"(y) : "st"); printf("x87i %.17g\n", r);
        x += 0.5; y -= 0.75; fx += 1.0f; li += 7; si += 3;
    }
    /* fxsave / fxrstor / ldmxcsr / stmxcsr */
    { static unsigned char area[512] __attribute__((aligned(16))); unsigned mx;
      __m128 keep = F(1, 1); __asm__ volatile("fxsave %0" : "=m"(area) :: "memory");
      printf("fxsave fcw=%02x%02x mxcsr=%02x%02x%02x%02x\n", area[1], area[0], area[27], area[26], area[25], area[24] & 0xc0);
      { unsigned char xm[16]; memcpy(xm, area + 160, 16); (void) xm; }
      __asm__ volatile("fxrstor %0" :: "m"(area) : "memory"); prf("after-fxrstor", keep);
      __asm__ volatile("stmxcsr %0" : "=m"(mx)); printf("mxcsr %x\n", mx & 0xffc0);
      mx = 0x1fa0; __asm__ volatile("ldmxcsr %0" :: "m"(mx)); __asm__ volatile("stmxcsr %0" : "=m"(mx)); printf("mxcsr2 %x\n", mx & 0xffff); mx = 0x1f80; __asm__ volatile("ldmxcsr %0" :: "m"(mx)); }
    return 0;
}

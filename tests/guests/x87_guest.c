/* tests/guests/x87_guest.c - x87 behaviour that must match real hardware: native run vs the pemu interpreter (tests/integration/x87.sh).
 * Prints raw bytes / words only (no long double formatting, so the output does not depend on libc).  x86_64 only. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>

static void hex80(const char *tag, const void *p) {
    const unsigned char *b = p; printf("%s", tag);
    for (int i = 9; i >= 0; i--) printf("%02x", b[i]);
    printf("\n");
}
static void w(const char *tag, unsigned v) { printf("%s %04x\n", tag, v & 0xffff); }

/* 1. denormals survive a store as 80-bit (they used to be flushed to zero) */
static void t_denormal(void) {
    unsigned char in[10] = { 0x01, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00 };      /* smallest 80-bit denormal: 2^-16445 */
    unsigned char out[10]; memset(out, 0xaa, 10);
    __asm__ volatile("fldt %1; fstpt %0" : "=m"(out) : "m"(in) : "st");
    hex80("denorm.min ", out);
    unsigned char in2[10] = { 0, 0, 0, 0, 0, 0, 0, 0x40, 0x00, 0x00 };     /* 2^-16383 * 2^62-ish denormal */
    __asm__ volatile("fldt %1; fstpt %0" : "=m"(out) : "m"(in2) : "st");
    hex80("denorm.mid ", out);
    long double a = 3.0e-4940L, b = 2.0L; unsigned char o3[10];            /* result of a multiply lands in the denormal range */
    __asm__ volatile("fldt %1; fldt %2; fmulp; fstpt %0" : "=m"(o3) : "m"(a), "m"(b) : "st");
    hex80("denorm.mul ", o3);
}

/* 2. fprem / fprem1: remainder, quotient bits in C0 C3 C1, C2 cleared */
static void t_fprem(void) {
    static const double pairs[][2] = { { 17.0, 5.0 }, { -17.0, 5.0 }, { 100.0, 7.0 }, { 5.5, 2.0 }, { 3.0, 4.0 }, { 63.0, 8.0 }, { 1e3, 7.0 } };
    for (unsigned i = 0; i < sizeof pairs / sizeof *pairs; i++) {
        long double a = pairs[i][0], b = pairs[i][1]; unsigned char r[10]; unsigned short sw;
        __asm__ volatile("fldt %2; fldt %3; fprem; fnstsw %1; fstp %%st(1); fstpt %0" : "=m"(r), "=a"(sw) : "m"(b), "m"(a) : "st", "st(1)");
        printf("fprem %u ", i); hex80("", r); w(" sw", sw & 0x4700);
        __asm__ volatile("fldt %2; fldt %3; fprem1; fnstsw %1; fstp %%st(1); fstpt %0" : "=m"(r), "=a"(sw) : "m"(b), "m"(a) : "st", "st(1)");
        printf("fprem1 %u ", i); hex80("", r); w(" sw", sw & 0x4700);
    }
}

/* 3. precision control: 24 / 53 / 64 bit significand for add, mul, div, sqrt */
static void t_pc(void) {
    static const unsigned short pc[] = { 0x007f, 0x027f, 0x037f };          /* single, double, extended; all exceptions masked, round to nearest */
    for (int k = 0; k < 3; k++) {
        unsigned short cw = pc[k], save; unsigned char r[10]; long double one = 1.0L, three = 3.0L, e = 1.0e-10L;
        __asm__ volatile("fnstcw %0" : "=m"(save));
        __asm__ volatile("fldcw %0" : : "m"(cw));
        __asm__ volatile("fldt %1; fldt %2; fdivrp; fstpt %0" : "=m"(r) : "m"(three), "m"(one) : "st");   /* 1/3 */
        printf("pc%d div ", k); hex80("", r);
        __asm__ volatile("fldt %1; fldt %2; faddp; fstpt %0" : "=m"(r) : "m"(one), "m"(e) : "st");        /* 1 + 1e-10 */
        printf("pc%d add ", k); hex80("", r);
        __asm__ volatile("fldt %1; fldt %2; fmulp; fstpt %0" : "=m"(r) : "m"(three), "m"(e) : "st");
        printf("pc%d mul ", k); hex80("", r);
        __asm__ volatile("fldt %1; fsqrt; fstpt %0" : "=m"(r) : "m"(three) : "st");
        printf("pc%d sqrt ", k); hex80("", r);
        __asm__ volatile("fldcw %0" : : "m"(save));
    }
}

/* 4. tag word: fnstenv shows which registers hold a value */
static void t_tag(void) {
    unsigned char env[28]; unsigned short tag;
    __asm__ volatile("fninit; fnstenv %0; fldenv %0" : "=m"(env));
    memcpy(&tag, env + 8, 2); w("tag.init ", tag);
    __asm__ volatile("fninit; fld1; fldz; fnstenv %0; fldenv %0; fstp %%st(0); fstp %%st(0)" : "=m"(env) : : "st", "st(1)");
    memcpy(&tag, env + 8, 2); w("tag.two ", tag);              /* fld1 -> valid, fldz -> zero (01) */
    __asm__ volatile("fninit; fld1; fldpi; ffree %%st(0); fnstenv %0; fninit" : "=m"(env) : : "st", "st(1)");
    memcpy(&tag, env + 8, 2); w("tag.free ", tag);
    __asm__ volatile("fninit; fld1; fld1; fld1; fstp %%st(0); fnstenv %0; fninit" : "=m"(env) : : "st", "st(1)", "st(2)");
    memcpy(&tag, env + 8, 2); w("tag.pop ", tag);
    unsigned char fx[512] __attribute__((aligned(16)));
    __asm__ volatile("fninit; fld1; fldz; fxsave %0; fninit" : "=m"(fx) : : "st", "st(1)", "memory");
    printf("fxsave.ftw %02x\n", fx[4]);
}

/* 5. instructions that were missing */
static void t_missing(void) {
    unsigned short sw; unsigned char env[28];
    __asm__ volatile("fninit; fld1; fld1; .byte 0xdf, 0xc0; fnstsw %0" : "=a"(sw) : : "st", "st(1)");          /* ffreep %st(0): free and pop */
    w("ffreep.top ", sw & 0x3800);
    __asm__ volatile("fninit; fld1; fld1; .byte 0xdf, 0xc0; fnstenv %0; fninit" : "=m"(env) : : "st", "st(1)");
    { unsigned short t; memcpy(&t, env + 8, 2); w("ffreep.tag ", t); }
    long double a = 7.0L; unsigned char r[10];
    __asm__ volatile("fldt %1; fldz; .byte 0xdd, 0xd1; fstp %%st(0); fstpt %0" : "=m"(r) : "m"(a) : "st", "st(1)");  /* fst %st(1) */
    hex80("fst.st1 ", r);
    __asm__ volatile("fninit; .byte 0xdb, 0xe0, 0xdb, 0xe1, 0xdb, 0xe4; fnstcw %0" : "=m"(sw));                   /* fneni fndisi fnsetpm: nops */
    w("nop3.fcw ", sw);
    __asm__ volatile("fninit; fld1; fldz; fcompp; fnstsw %0" : "=a"(sw) : : "st", "st(1)");
    w("fcompp.sw ", sw & 0x4700);
}


/* 6. exception flags (IE DE ZE OE UE PE SF, C1 = rounded up) after arithmetic on special operands; all exceptions masked */
typedef struct { uint64_t m; uint16_t se; } V80;
static const V80 sv[] = {
    { 0, 0x0000 }, { 0, 0x8000 }, { 0x8000000000000000ULL, 0x3fff }, { 0xC000000000000000ULL, 0x4000 }, { 0xAAAAAAAAAAAAAAABULL, 0x3ffd },
    { 1, 0 }, { 0x8000000000000000ULL, 1 }, { 0xFFFFFFFFFFFFFFFFULL, 0x7ffe }, { 0x8000000000000000ULL, 0x7fff }, { 0x8000000000000000ULL, 0xffff },
    { 0xC000000000000001ULL, 0x7fff }, { 0xA000000000000000ULL, 0x7fff }, { 0xC000000000000000ULL, 0xffff }, { 0x8000000000000000ULL, 0x0100 }, { 0x9000000000000000ULL, 0x3f80 },
};
#define NSV (int)(sizeof sv / sizeof *sv)
static void fl_line(const char *t, int a, int b, const void *r, unsigned sw) {
    const unsigned char *p = r; printf("%s %d %d ", t, a, b);
    if (p) for (int i = 9; i >= 0; i--) printf("%02x", p[i]); else printf("-");
    printf(" %04x\n", sw & 0xffff);
}
#define FLBIN(nm, ins) for (int a = 0; a < NSV; a++) for (int b = 0; b < NSV; b++) { unsigned char r[16] = {0}; unsigned short sw; \
    __asm__ volatile("fninit; fldt %2; fldt %3; " ins "; fnstsw %1; fstpt %0; fninit" : "=m"(r), "=a"(sw) : "m"(sv[b]), "m"(sv[a]) : "st", "st(1)"); fl_line(nm, a, b, r, sw); }
#define FLUN(nm, ins) for (int a = 0; a < NSV; a++) { unsigned char r[16] = {0}; unsigned short sw; \
    __asm__ volatile("fninit; fldt %2; " ins "; fnstsw %1; fstpt %0; fninit" : "=m"(r), "=a"(sw) : "m"(sv[a]) : "st", "st(1)"); fl_line(nm, a, 0, r, sw); }
static void t_flags(void) {
    FLBIN("fadd", "faddp") FLBIN("fsub", "fsubp") FLBIN("fmul", "fmulp") FLBIN("fdiv", "fdivp") FLBIN("fdivr", "fdivrp")
    FLUN("fsqrt", "fsqrt") FLUN("frndint", "frndint") FLUN("fchs", "fchs") FLUN("fxam", "fxam")
    for (int a = 0; a < NSV; a++) for (int b = 0; b < NSV; b++) { unsigned short sw;
        __asm__ volatile("fninit; fldt %1; fldt %2; fcom %%st(1); fnstsw %0; fninit" : "=a"(sw) : "m"(sv[b]), "m"(sv[a]) : "st", "st(1)"); fl_line("fcom", a, b, 0, sw);
        __asm__ volatile("fninit; fldt %1; fldt %2; fucom %%st(1); fnstsw %0; fninit" : "=a"(sw) : "m"(sv[b]), "m"(sv[a]) : "st", "st(1)"); fl_line("fucom", a, b, 0, sw); }
    for (int a = 0; a < NSV; a++) { int o32 = 0; short o16 = 0; unsigned short sw; float f = 0; double d = 0;
        __asm__ volatile("fninit; fldt %2; fistpl %1; fnstsw %0" : "=a"(sw), "=m"(o32) : "m"(sv[a]) : "st"); printf("fist32 %d %08x %04x\n", a, o32, sw);
        __asm__ volatile("fninit; fldt %2; fisttps %1; fnstsw %0" : "=a"(sw), "=m"(o16) : "m"(sv[a]) : "st"); printf("fisttp16 %d %04x %04x\n", a, (unsigned short) o16, sw);
        __asm__ volatile("fninit; fldt %2; fstps %1; fnstsw %0" : "=a"(sw), "=m"(f) : "m"(sv[a]) : "st"); { unsigned u; memcpy(&u, &f, 4); printf("fst32 %d %08x %04x\n", a, u, sw); }
        __asm__ volatile("fninit; fldt %2; fstpl %1; fnstsw %0" : "=a"(sw), "=m"(d) : "m"(sv[a]) : "st"); { unsigned long long u; memcpy(&u, &d, 8); printf("fst64 %d %016llx %04x\n", a, u, sw); } }
    unsigned short sw; unsigned char r[16];
    __asm__ volatile("fninit; fstp %%st(0); fnstsw %0" : "=a"(sw) : : "st"); w("pop.empty ", sw);
    __asm__ volatile("fninit; fld1;fld1;fld1;fld1;fld1;fld1;fld1;fld1; fld1; fnstsw %0; fstpt %1; fninit" : "=a"(sw), "=m"(r) : : "st"); fl_line("push9", 0, 0, r, sw);
    __asm__ volatile("fninit; fldz; fld1; fdivp; fnclex; fnstsw %0; fninit" : "=a"(sw) : : "st", "st(1)"); w("fnclex ", sw);
}

/* 7. sin / cos / tan reduce with the CPU's 66-bit pi: results near multiples of pi/2 differ from libm exactly as the hardware does; |x| >= 2^63 sets C2 and changes nothing */
static void t_trig(void) {
    const long double pi = 3.14159265358979323846264338327950288L;
    for (int k = 1; k < 100; k++) {
        long double x = k * pi / 2 + (k % 7 - 3) * 1e-3L, a = x, s, c; unsigned short sw;
        __asm__ volatile("fsin; fnstsw %1; fstpt %0" : "=m"(s), "=a"(sw) : "t"(a) : "st"); fl_line("sin", k, 0, &s, sw & 0x4500);
        __asm__ volatile("fcos; fnstsw %1; fstpt %0" : "=m"(c), "=a"(sw) : "t"(a) : "st"); fl_line("cos", k, 0, &c, sw & 0x4500);
    }
    for (int k = 1; k < 60; k++) { long double x = k * pi, s; unsigned short sw;
        __asm__ volatile("fsin; fnstsw %1; fstpt %0" : "=m"(s), "=a"(sw) : "t"(x) : "st"); fl_line("sinpi", k, 0, &s, sw & 0x4500); }
    static const long double big[] = { 1e5L, 1e10L, 1e15L, 1e18L, 9.2e18L, 9.3e18L, 1e19L, 1e30L, -1e18L, -1e19L };
    for (unsigned i = 0; i < sizeof big / sizeof *big; i++) { long double s, c, t1; unsigned short sw, sw2, sw3;
        __asm__ volatile("fsin; fnstsw %1; fstpt %0" : "=m"(s), "=a"(sw) : "t"(big[i]) : "st"); fl_line("sinbig", i, 0, &s, sw & 0x4500);
        __asm__ volatile("fcos; fnstsw %1; fstpt %0" : "=m"(c), "=a"(sw2) : "t"(big[i]) : "st"); fl_line("cosbig", i, 0, &c, sw2 & 0x4500);
        /* fptan is not correctly rounded and its last bit differs between CPU models: only its status word is compared */
        __asm__ volatile("fptan; fnstsw %2; fstp %%st(0); fstpt %0; fninit" : "=m"(t1), "=m"(s), "=a"(sw3) : "t"(big[i]) : "st", "st(1)"); (void) t1; fl_line("tanbig", i, 0, 0, sw3 & 0x4500); }
}

/* 8. an unmasked exception is raised by the next waiting instruction as SIGFPE with the matching si_code (divide, invalid, overflow); masked: nothing happens */
static sigjmp_buf jb;
static void on_fpe(int sig, siginfo_t *si, void *u) { (void) u; printf("SIGFPE %d code %d\n", sig, si->si_code); siglongjmp(jb, 1); }
static void t_trap(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_fpe; sa.sa_flags = SA_SIGINFO; sigaction(SIGFPE, &sa, 0);
    static const unsigned short cw_zm = 0x037b, cw_im = 0x037e, cw_om = 0x0377;           /* ZE / IE / OE unmasked */
    volatile long double one = 1.0L, zero = 0.0L, big = 1e4000L, inf = 1.0L / zero;
    if (!sigsetjmp(jb, 1)) { __asm__ volatile("fninit; fldcw %0; fldt %1; fldt %2; fdivp; fwait" : : "m"(cw_zm), "m"(zero), "m"(one) : "st", "st(1)"); puts("no signal (ze)"); }
    __asm__ volatile("fninit");
    if (!sigsetjmp(jb, 1)) { __asm__ volatile("fninit; fldcw %0; fldt %1; fldt %2; fsubrp; fwait; fnop" : : "m"(cw_im), "m"(inf), "m"(inf) : "st", "st(1)"); puts("no signal (ie)"); }
    __asm__ volatile("fninit");
    if (!sigsetjmp(jb, 1)) { __asm__ volatile("fninit; fldcw %0; fldt %1; fldt %1; fmulp; fnop" : : "m"(cw_om), "m"(big) : "st", "st(1)"); puts("no signal (oe)"); }
    __asm__ volatile("fninit");
    unsigned short sw;
    __asm__ volatile("fninit; fldz; fld1; fdivp; fwait; fnstsw %0; fninit" : "=a"(sw) : : "st", "st(1)"); w("masked.ze ", sw);            /* masked: just the flag */
}

/* 9. random normal operands through + - * / under all three precision controls: value and the whole status word (PE, C1) must match the hardware */
static uint64_t fz_s = 0x9E3779B97F4A7C15ULL;
static uint64_t fz_r(void) { fz_s ^= fz_s << 13; fz_s ^= fz_s >> 7; fz_s ^= fz_s << 17; return fz_s; }
static V80 fz_mk(void) {
    V80 v; int k = (int)(fz_r() % 6); v.m = fz_r() | 0x8000000000000000ULL;
    if (k == 0) v.m &= 0xFFFFFFFF00000000ULL; if (k == 0) v.m |= 0x8000000000000000ULL;
    if (k == 1) v.m = 0x8000000000000000ULL | (fz_r() & 0xff);
    int e = (int)(fz_r() % 40) + 16383 - 20;
    v.se = (uint16_t)(e | ((fz_r() & 1) ? 0x8000 : 0)); return v;
}
static void t_fuzz(void) {
    static const unsigned short cws[] = { 0x037f, 0x027f, 0x007f };
    for (int n = 0; n < 400; n++) { V80 a = fz_mk(), b = fz_mk();
        for (int k = 0; k < 3; k++) { unsigned short cw = cws[k]; unsigned char r[16]; unsigned short sw;
#define FZ(nm, ins) __asm__ volatile("fninit; fldcw %4; fldt %2; fldt %3; " ins "; fnstsw %1; fstpt %0; fninit" : "=m"(r), "=a"(sw) : "m"(a), "m"(b), "m"(cw) : "st", "st(1)"); fl_line(nm, n, k, r, sw);
            FZ("zadd", "faddp") FZ("zsub", "fsubp") FZ("zmul", "fmulp") FZ("zdiv", "fdivp") FZ("zdivr", "fdivrp") } }
}


/* 10. rounding control (down / up / toward zero) in + - * / and fsqrt, under every precision control, and in fst m32 / m64: operands over the whole exponent range, so
 * overflow (largest finite number instead of infinity), denormal results and the sign of an exact zero are covered; value and the whole status word must match */
static V80 fz_mkx(void) {
    static const int lo[6] = { 0, 0x7fe0, 0x2000, 0x5fe0, 0x3ff0, 0x3fc0 }; static const int sp[6] = { 24, 31, 64, 32, 32, 64 };
    int k = (int)(fz_r() % 6); V80 v; v.m = fz_r() | 0x8000000000000000ULL;
    int j = (int)(fz_r() % 7); if (j == 0) v.m = 0x8000000000000000ULL | (fz_r() & 3); else if (j == 1) v.m |= 0x7fffffffffffULL, v.m &= 0xFFFFFFFFFFFF0000ULL | 0x7fffffffffffULL;
    int e = lo[k] + (int)(fz_r() % (unsigned) sp[k]); if (e > 0x7ffe) e = 0x7ffe;
    if (e == 0) v.m &= 0x7fffffffffffffffULL;
    v.se = (uint16_t)(e | ((fz_r() & 1) ? 0x8000 : 0)); return v;
}
static void t_rc(void) {
    static const unsigned char pcs[] = { 3, 2, 0 };
    for (int n = 0; n < 1500; n++) { V80 a = (n & 1) ? fz_mkx() : fz_mk(), b = (n & 2) ? fz_mkx() : fz_mk();
        if (n % 17 == 0) { b = a; b.se ^= 0x8000; }                                  /* x + (-x) */
        for (int rc = 0; rc < 4; rc++) for (int k = 0; k < 3; k++) { unsigned short cw = (unsigned short)(0x3f | (pcs[k] << 8) | (rc << 10)); unsigned char r[16]; unsigned short sw;
#define RZ(nm, ins) __asm__ volatile("fninit; fldcw %4; fldt %2; fldt %3; " ins "; fnstsw %1; fstpt %0; fninit" : "=m"(r), "=a"(sw) : "m"(a), "m"(b), "m"(cw) : "st", "st(1)"); fl_line(nm, n, rc * 4 + k, r, sw);
            RZ("radd", "faddp") RZ("rsub", "fsubp") RZ("rmul", "fmulp") RZ("rdiv", "fdivp") RZ("rdivr", "fdivrp") RZ("rsqrt", "fxch; fsqrt; fxch; faddp %%st, %%st(1); fsubp %%st, %%st(1)")
        } }
    for (int n = 0; n < 600; n++) { V80 a = (n & 1) ? fz_mkx() : fz_mk(); if (n < 300 && (a.se & 0x7fff) > 0x4100 && (a.se & 0x7fff) < 0x7000) a.se = (uint16_t)((a.se & 0x8000) | (0x3f80 + (n % 0x200)));
        for (int rc = 0; rc < 4; rc++) { unsigned short cw = (unsigned short)(0x37f | (rc << 10)), sw; float f = 0; double d = 0; unsigned u; unsigned long long ul;
            __asm__ volatile("fninit; fldcw %3; fldt %2; fstps %1; fnstsw %0" : "=a"(sw), "=m"(f) : "m"(a), "m"(cw) : "st"); memcpy(&u, &f, 4); printf("rfst32 %d %d %08x %04x\n", n, rc, u, sw);
            __asm__ volatile("fninit; fldcw %3; fldt %2; fstpl %1; fnstsw %0" : "=a"(sw), "=m"(d) : "m"(a), "m"(cw) : "st"); memcpy(&ul, &d, 8); printf("rfst64 %d %d %016llx %04x\n", n, rc, ul, sw); } }
    for (int n = 0; n < 200; n++) { V80 a = fz_mkx(); a.se &= 0x7fff; a.se = (uint16_t)(a.se % 0x7ffe);                   /* fsqrt of a positive value, all rounding modes */
        for (int rc = 0; rc < 4; rc++) for (int k = 0; k < 3; k++) { unsigned short cw = (unsigned short)(0x3f | (pcs[k] << 8) | (rc << 10)); unsigned char r[16]; unsigned short sw;
            __asm__ volatile("fninit; fldcw %3; fldt %2; fsqrt; fnstsw %1; fstpt %0; fninit" : "=m"(r), "=a"(sw) : "m"(a), "m"(cw) : "st"); fl_line("rsq", n, rc * 4 + k, r, sw); } }
}

/* 11. an unmasked IE / ZE / DE is detected before the operation: the destination and the stack are left alone. fnsave does not wait, so the register stack can be read
 * back while the exception is still pending (control word, status word, tag word and the eight registers are compared) */
static void sv_line(const char *t, int a, int b, int m, const unsigned char *buf) {
    printf("%s %d %d %d %04x %04x %04x ", t, a, b, m, *(const unsigned short *) buf, *(const unsigned short *)(buf + 4), *(const unsigned short *)(buf + 8));
    unsigned sw = *(const unsigned short *)(buf + 4), tw = *(const unsigned short *)(buf + 8), top = (sw >> 11) & 7;
    for (unsigned i = 0; i < 8; i++) {                                                   /* an empty register holds whatever was there before: not compared */
        if (((tw >> (2 * ((top + i) & 7))) & 3) == 3) { printf("-"); continue; }
        for (int j = 9; j >= 0; j--) printf("%02x", buf[28 + 10 * i + j]);
        printf(" "); }
    printf("\n");
}
static void t_unmasked(void) {
    static const unsigned short cws[3] = { 0x037e, 0x037b, 0x037d };                         /* IE / ZE / DE unmasked */
    static const float fs[2] = { 1.5f, 0.0f };
    for (int a = 0; a < NSV; a++) for (int b = 0; b < NSV; b++) for (int m = 0; m < 3; m++) { unsigned short cw = cws[m]; unsigned char buf[112];
#define UZ(nm, ins) memset(buf, 0, sizeof buf); __asm__ volatile("fninit; fldcw %2; fldt %0; fldt %1; " ins "; fnsave %3; fninit" : : "m"(sv[b]), "m"(sv[a]), "m"(cw), "m"(buf), "m"(fs[0]) : "st", "st(1)", "memory"); sv_line(nm, a, b, m, buf);
        UZ("uadd", "faddp") UZ("usub", "fsubp") UZ("umul", "fmulp") UZ("udiv", "fdivp") UZ("udivr", "fdivrp") UZ("uaddn", "fadd %%st(1), %%st") UZ("udivn", "fdiv %%st(1), %%st")
        UZ("udivt", "fdiv %%st, %%st(1)") UZ("usqrt", "fsqrt") UZ("ucomp", "fcomp %%st(1)") UZ("ucompp", "fcompp")
        UZ("uadds", "fadds %4") UZ("udivs", "fdivs %4") }
}
#define SZ(nm, ins) memset(buf, 0, sizeof buf); __asm__ volatile("fninit; fldcw %2; fldt %0; fldt %1; " ins "; fnsave %3; fninit" : : "m"(b), "m"(a), "m"(cw), "m"(buf) : "st", "st(1)", "memory"); sv_line(nm, n, m * 16 + rc * 4 + k, 0, buf);

/* 12. an unmasked OE / UE: the instruction completes with the rounded result whose exponent is brought back (-24576 for overflow, +24576 for underflow), the
 * exception is pending. Operands over the whole exponent range; read back with fnsave (cw, sw, tw and the registers in use). */
static void t_scaled(void) {
    static const unsigned short cws[3] = { 0x0377, 0x036f, 0x0367 };                         /* OE / UE / both unmasked, precision and rounding set below */
    static const unsigned char pcs[] = { 3, 2, 0 };
    for (int n = 0; n < 1200; n++) { V80 a = (n & 1) ? fz_mkx() : fz_mk(), b = (n & 2) ? fz_mkx() : fz_mk();
        for (int m = 0; m < 3; m++) for (int rc = 0; rc < 4; rc++) for (int k = 0; k < 3; k++) { unsigned short cw = (unsigned short)((cws[m] & ~0xf00) | (pcs[k] << 8) | (rc << 10)); unsigned char buf[112];
            SZ("saddp", "faddp") SZ("ssubp", "fsubp") SZ("smulp", "fmulp") SZ("sdivp", "fdivp") SZ("sdivrp", "fdivrp") } }
}

/* 13. stores with an unmasked exception: fst m32 / m64 and fist(p) / fisttp m16 / m32 / m64. Memory starts with a marker; memory, the status word, the tag word and
 * the stack (fnsave, which does not wait) are compared, so a store that must not happen, or a pop that must not happen, shows up. */
static void t_ustore(void) {
    static const unsigned short cws[4] = { 0x037e, 0x0377, 0x036f, 0x035f };                 /* IE / OE / UE / PE unmasked */
    for (int n = 0; n < NSV + 700; n++) { V80 a = n < NSV ? sv[n] : (n & 1) ? fz_mkx() : fz_mk();
        if (n >= NSV && (n & 2)) { a.se = (uint16_t)((a.se & 0x8000) | (0x3f80 + (fz_r() % 0x100))); a.m |= 0x8000000000000000ULL; }          /* around the float / double range */
        if (n >= NSV && (n & 4)) { a.se = (uint16_t)((a.se & 0x8000) | (0x3fe0 + (fz_r() % 0x30))); a.m |= 0x8000000000000000ULL; }           /* around the int range */
        for (int m = 0; m < 4; m++) { unsigned short cw = cws[m]; unsigned char buf[112]; unsigned char mem[8];
#define STZ(nm, ins) memset(buf, 0, sizeof buf); memset(mem, 0x5a, sizeof mem); __asm__ volatile("fninit; fldcw %1; fldt %0; fld1; fxch; " ins " %2; fnsave %3; fninit" : : "m"(a), "m"(cw), "m"(mem), "m"(buf) : "st", "st(1)", "memory"); \
        printf("%s %d %d ", nm, n, m); for (int i = 0; i < 8; i++) printf("%02x", mem[i]); printf(" "); sv_line("", n, m, 0, buf);
            STZ("zfsts", "fsts") STZ("zfstps", "fstps") STZ("zfstl", "fstl") STZ("zfstpl", "fstpl")
            STZ("zfists", "fists") STZ("zfistpl", "fistpl") STZ("zfistpq", "fistpll") STZ("zfisttps", "fisttps") STZ("zfisttpl", "fisttpl") } }
}

/* 14. fpatan honours the rounding control. The CPU is not correctly rounded for it (a few percent of the results are one ulp off, and so is C1), so only what is exact
 * on the hardware is compared: the multiples of pi/4 (zero / infinite arguments, all four rounding modes), a tiny quotient whose nearest result is z itself (x a power of two:
 * z is exact, also in the denormal range) and, on random arguments, that the four rounding modes agree with each other (down <= nearest <= up, at most one ulp apart, and down != up for nearly all of them,
 * toward zero is one of the two, inexact flag). */
static void patan1(V80 y, V80 x, int rc, unsigned char *o, unsigned short *sw) {
    unsigned short cw = (unsigned short)(0x037f | (rc << 10)); unsigned short t;
    __asm__ volatile("fninit; fldcw %3; fldt %2; fldt %1; fpatan; fnstsw %0; fstpt %4; fninit" : "=a"(t) : "m"(x), "m"(y), "m"(cw), "m"(*(unsigned char (*)[10]) o) : "st", "st(1)", "memory");
    *sw = (unsigned short)(t & 0x023f);
}
static __int128 pkey(const unsigned char *o) {                                         /* order key of an 80-bit value: neighbours differ by 1 */
    uint64_t m; uint16_t se; memcpy(&m, o, 8); memcpy(&se, o + 8, 2);
    int e = se & 0x7fff; __int128 k = e ? ((__int128) e << 63) + (__int128)(m - 0x8000000000000000ULL) : (__int128) m;
    return (se & 0x8000) ? -k : k;
}
#define FINNZ(k) ((k) == 2 || (k) == 3 || (k) >= 6)                                    /* pv[k] is finite and not zero */
static void t_patan(void) {
    static const V80 pv[] = { { 0, 0 }, { 0, 0x8000 }, { 0x8000000000000000ULL, 0x3fff }, { 0x8000000000000000ULL, 0xbfff }, { 0x8000000000000000ULL, 0x7fff }, { 0x8000000000000000ULL, 0xffff },
        { 1, 0 }, { 1, 0x8000 }, { 0x8000000000000000ULL, 1 }, { 0xffffffffffffffffULL, 0x7ffe }, { 0xc000000000000000ULL, 0x4000 }, { 0xffffffffffffffffULL, 0xfffe } };
    unsigned char o[10]; unsigned short sw;
    for (unsigned i = 0; i < sizeof pv / sizeof *pv; i++) for (unsigned j = 0; j < sizeof pv / sizeof *pv; j++) for (int rc = 0; rc < 4; rc++) {
        if (FINNZ(i) && FINNZ(j)) continue;                                              /* (finite, finite) is not exact: compared by property below, random arguments after that */
        patan1(pv[i], pv[j], rc, o, &sw); printf("patan.sp %u %u %d ", i, j, rc); for (int b = 9; b >= 0; b--) printf("%02x", o[b]); printf(" %04x\n", sw); }
    for (unsigned i = 0; i < sizeof pv / sizeof *pv; i++) for (unsigned j = 0; j < sizeof pv / sizeof *pv; j++) {      /* (finite non-zero, finite non-zero): the result is atan2 = an irrational angle, or z minus a tiny */
        if (!(FINNZ(i) && FINNZ(j))) continue;                                           /* correction. The CPUs differ here (one returns z itself in every mode, another rounds z - eps in the mode), so */
        unsigned char r[4][10]; unsigned short s[4];                                     /* only what holds for all of them is printed: the four modes are consistent with each other */
        for (int rc = 0; rc < 4; rc++) patan1(pv[i], pv[j], rc, r[rc], &s[rc]);
        __int128 nr = pkey(r[0]), dn = pkey(r[1]), up = pkey(r[2]), tzx = (r[3][9] & 0x80) ? up : dn;
        printf("patan.fin %u %u %d %d %d\n", i, j, dn <= nr && nr <= up, up - dn <= 1, pkey(r[3]) == tzx); }
    for (int n = 0; n < 1500; n++) {                                                     /* tiny exact quotient */
        V80 y, x; y.m = fz_r() | 0x8000000000000000ULL; int ex = 16383 + 40 + (int)(fz_r() % 16300), ey = ex - 45 - (int)(fz_r() % 16450);
        if (n % 5 == 0) { ey = 0; y.m &= 0x7fffffffffffffffULL; if (!y.m) y.m = 1; }
        if (ey < 0) ey = 0;
        y.se = (uint16_t)(ey | ((fz_r() & 1) ? 0x8000 : 0)); x.m = 0x8000000000000000ULL; x.se = (uint16_t) ex;
        unsigned char r[4][10]; unsigned short s[4];                                     /* the CPUs differ in the directed modes (one returns z in all of them, another rounds z - eps: */
        for (int rc = 0; rc < 4; rc++) patan1(y, x, rc, r[rc], &s[rc]);                  /* z or the neighbour towards zero, with C1), so print only what they share: the nearest result */
        __int128 nr = pkey(r[0]), dn = pkey(r[1]), up = pkey(r[2]), tzx = (r[3][9] & 0x80) ? up : dn;       /* and PE / UE / DE when it is normal, and the consistency of the four modes */
        printf("patan.tiny %d %d %d %d ", n, dn <= nr && nr <= up, up - dn <= 1, pkey(r[3]) == tzx);
        if (r[0][8] | (r[0][9] & 0x7f)) { for (int b = 9; b >= 0; b--) printf("%02x", r[0][b]); printf(" %04x\n", s[0] & 0x3f); } else printf("den\n"); }
    int split = 0;
    for (int n = 0; n < 3000; n++) {                                                     /* random: the rounding modes against each other */
        V80 y = fz_mk(), x = fz_mk(); unsigned char r[4][10]; unsigned short s[4];
        for (int rc = 0; rc < 4; rc++) patan1(y, x, rc, r[rc], &s[rc]);
        __int128 nr = pkey(r[0]), dn = pkey(r[1]), up = pkey(r[2]), tz = pkey(r[3]);
        __int128 tzx = (r[3][9] & 0x80) ? up : dn;                                       /* toward zero: up for a negative result, down for a positive one */
        split += dn != up;
        printf("patan.rnd %d %d %d %d %d\n", n, dn <= nr && nr <= up, up - dn <= 1, tz == tzx, (s[0] & 0x20) != 0); }
    printf("patan.split %d\n", split >= 2700);                                     /* the CPU: 2988 of 3000 (an exact result on the grid is the rest) */
}
/* 15. an addition / subtraction with a denormal operand: it leaves the fast path, so the general path must cope with a result of any size (a huge operand with directed
 * rounding came out as zero), and an unmasked OE gives the rounded result with its exponent brought back by 2^-24576. Control word variants: all masked, OE, OE and UE, UE. */
static void t_addden(void) {
    static const unsigned short cws[4] = { 0x037f, 0x0377, 0x0367, 0x036f };
    static const unsigned char pcs[] = { 3, 2, 0 };
    for (int n = 0; n < 300; n++) { V80 a, b; a.m = (n % 3 == 0) ? 0xffffffffffffffffULL : (fz_r() | 0x8000000000000000ULL);
        int e = (n % 2) ? 0x7ffe - (int)(fz_r() % 3) : (int)(fz_r() % 0x7ffe) + 1; a.se = (uint16_t)(e | ((fz_r() & 1) ? 0x8000 : 0));
        b.m = (fz_r() % 0x7fffffffffffffffULL) + 1; b.se = (uint16_t)((fz_r() & 1) ? 0x8000 : 0);                  /* a denormal */
        for (int m = 0; m < 4; m++) for (int rc = 0; rc < 4; rc++) for (int k = 0; k < 3; k++) { unsigned short cw = (unsigned short)((cws[m] & ~0xf00) | (pcs[k] << 8) | (rc << 10)); unsigned char buf[112];
            SZ("daddp", "faddp") SZ("dsubp", "fsubp") SZ("dsubrp", "fsubrp") } }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    t_denormal(); t_fprem(); t_pc(); t_tag(); t_missing();
    t_flags(); t_trig(); t_trap(); t_fuzz(); t_rc(); t_unmasked(); t_scaled(); t_ustore(); t_patan(); t_addden();
    return 0;
}

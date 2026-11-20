/* JIT test: instructions translated as helper calls (cpuid / rdtsc / xgetbv, string instructions). Lines that start with '~' differ natively by design. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef unsigned long long u64;
static unsigned char a[64], b[64];
static void fill(void) { for (int i = 0; i < 64; i++) { a[i] = (unsigned char)(i * 7 + 3); b[i] = (unsigned char)(i * 5 + 1); } }
static void dump(const char *t) { u64 h = 1469598103934665603ULL; for (int i = 0; i < 64; i++) { h ^= a[i]; h *= 1099511628211ULL; h ^= b[i]; h *= 1099511628211ULL; } printf("%s %016llx\n", t, h); }
#define MOVS(sfx, n, df) do { fill(); u64 c = n; void *d = b + 8, *s = a + 8; \
    __asm__ volatile(df "\n rep movs" sfx "\n cld" : "+c"(c), "+D"(d), "+S"(s) :: "memory"); \
    printf("movs" sfx " %s rcx=%llu rdi=%ld rsi=%ld\n", df, c, (long)((char*)d - (char*)b), (long)((char*)s - (char*)a)); dump("  mem"); } while (0)
#define MOVS1(sfx) do { fill(); void *d = b + 8, *s = a + 8; __asm__ volatile("movs" sfx : "+D"(d), "+S"(s) :: "memory"); printf("movs1" sfx " %ld %ld\n", (long)((char*)d - (char*)b), (long)((char*)s - (char*)a)); dump("  mem"); } while (0)
#define STOS(sfx, reg, v, n, df) do { fill(); u64 c = n; void *d = b + 16; u64 val = v; \
    __asm__ volatile(df "\n rep stos" sfx "\n cld" : "+c"(c), "+D"(d) : reg(val) : "memory"); \
    printf("stos" sfx " %s rcx=%llu rdi=%ld\n", df, c, (long)((char*)d - (char*)b)); dump("  mem"); } while (0)
static void cmps(void) {
    for (int k = 0; k < 4; k++) {
        fill(); if (k & 1) b[8 + 5] ^= 1; u64 c = 20; void *d = b + 8, *s = a + 8; int z, cf;
        memcpy(b + 8, a + 8, 20); if (k & 1) b[8 + 5] ^= 1;
        if (k & 2) __asm__ volatile("repe cmpsb\n setz %%r8b\n setc %%r9b\n movzbl %%r8b,%0\n movzbl %%r9b,%1" : "=r"(z), "=r"(cf), "+c"(c), "+D"(d), "+S"(s) :: "r8", "r9", "cc", "memory");
        else __asm__ volatile("repne cmpsb\n setz %%r8b\n setc %%r9b\n movzbl %%r8b,%0\n movzbl %%r9b,%1" : "=r"(z), "=r"(cf), "+c"(c), "+D"(d), "+S"(s) :: "r8", "r9", "cc", "memory");
        printf("cmpsb k=%d z=%d c=%d rcx=%llu\n", k, z, cf, c);
    }
}
static void scas(void) {
    char t[32]; memset(t, 'x', 32); t[11] = 'q';
    u64 c = 32; void *d = t; int z;
    __asm__ volatile("repne scasb\n setz %%r8b\n movzbl %%r8b,%0" : "=r"(z), "+c"(c), "+D"(d) : "a"((u64)'q') : "r8", "cc", "memory");
    printf("scasb z=%d rcx=%llu off=%ld\n", z, c, (long)((char*)d - t));
    c = 32; d = t; __asm__ volatile("repe scasb\n setz %%r8b\n movzbl %%r8b,%0" : "=r"(z), "+c"(c), "+D"(d) : "a"((u64)'x') : "r8", "cc", "memory");
    printf("scasb2 z=%d rcx=%llu off=%ld\n", z, c, (long)((char*)d - t));
    c = 4; d = t; __asm__ volatile("repne scasl\n setz %%r8b\n movzbl %%r8b,%0" : "=r"(z), "+c"(c), "+D"(d) : "a"(0x78787878u) : "r8", "cc", "memory");
    printf("scasd z=%d rcx=%llu off=%ld\n", z, c, (long)((char*)d - t));
}
static void lods(void) {
    fill(); void *s = a + 4; u64 r1, r2, r4, r8;
    __asm__ volatile("lodsb\n lodsw\n lodsl\n lodsq" : "+S"(s), "=a"(r8) :: "memory");
    __asm__ volatile("std\n lodsb\n cld" : "+S"(s), "=a"(r1) :: "memory");
    printf("lods %ld %llx %llx\n", (long)((char*)s - (char*)a), r8 & 0xff, r1 & 0xff); (void) r2; (void) r4;
}

#define FL(x) ((x) & 0x8d5)
/* flags are captured right after the instruction (live), or left dead (the result only) */
#define ADCSBB(name, ins, T, reg, a0, b0, cin) do { T r = a0; u64 fl; \
    __asm__ volatile((cin ? "stc\n" : "clc\n") ins "\n pushfq\n pop %1" : "+" reg(r), "=r"(fl) : "r"((T)(b0)) : "cc"); \
    printf(name " %x %x c%d -> %llx fl=%03llx\n", (unsigned)(a0), (unsigned)(b0), cin, (u64)(T)r, FL(fl)); } while (0)
static void narrow(void) {
    static const unsigned vs[] = { 0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x1234, 0xedcb };
    for (unsigned i = 0; i < 10; i++) for (unsigned k = 0; k < 10; k++) for (int c = 0; c < 2; c++) {
        unsigned a = vs[i], b = vs[k];
        { unsigned char r = a; u64 fl; unsigned char y = b;
          __asm__ volatile("bt $0, %%esi\n" "adcb %2, %0\n pushfq\n pop %1" : "+q"(r), "=r"(fl) : "q"(y), "S"(c) : "cc"); printf("adcb %x %x c%d %x %03llx\n", a & 255, b & 255, c, r, FL(fl)); }
        { unsigned char r = a; u64 fl; unsigned char y = b;
          __asm__ volatile("bt $0, %%esi\n" "sbbb %2, %0\n pushfq\n pop %1" : "+q"(r), "=r"(fl) : "q"(y), "S"(c) : "cc"); printf("sbbb %x %x c%d %x %03llx\n", a & 255, b & 255, c, r, FL(fl)); }
        { unsigned short r = a; u64 fl; unsigned short y = b;
          __asm__ volatile("bt $0, %%esi\n" "adcw %2, %0\n pushfq\n pop %1" : "+r"(r), "=r"(fl) : "r"(y), "S"(c) : "cc"); printf("adcw %x %x c%d %x %03llx\n", a & 0xffff, b & 0xffff, c, r, FL(fl)); }
        { unsigned short r = a; u64 fl; unsigned short y = b;
          __asm__ volatile("bt $0, %%esi\n" "sbbw %2, %0\n pushfq\n pop %1" : "+r"(r), "=r"(fl) : "r"(y), "S"(c) : "cc"); printf("sbbw %x %x c%d %x %03llx\n", a & 0xffff, b & 0xffff, c, r, FL(fl)); }
        { unsigned char m = a; u64 fl; unsigned char y = b;           /* memory destination */
          __asm__ volatile("bt $0, %%esi\n" "adcb %2, %0\n pushfq\n pop %1" : "+m"(m), "=r"(fl) : "q"(y), "S"(c) : "cc"); printf("adcb[m] %x %x c%d %x %03llx\n", a & 255, b & 255, c, m, FL(fl)); }
        { unsigned short r = a; unsigned short y = b; u64 fl;          /* flags dead: only the result */
          __asm__ volatile("bt $0, %%esi\n" "sbbw %1, %0" : "+r"(r) : "r"(y), "S"(c) : "cc"); printf("sbbw-dead %x\n", r); (void) fl; }
        { unsigned char r = a; u64 fl;
          __asm__ volatile("bt $0, %%esi\n" "adcb $0x95, %0\n pushfq\n pop %1" : "+q"(r), "=r"(fl) : "S"(c) : "cc"); printf("adcb-imm %x c%d %x %03llx\n", a & 255, c, r, FL(fl)); }
        { unsigned char r = a; u64 fl;
          __asm__ volatile("bt $0, %%esi\n" "sbbb $0x95, %%al\n pushfq\n pop %1" : "+a"(r), "=r"(fl) : "S"(c) : "cc"); printf("sbb-al-imm %x c%d %x %03llx\n", a & 255, c, r, FL(fl)); }
        { unsigned short r = a; u64 fl;
          __asm__ volatile("bt $0, %%esi\n" "adcw $-3, %0\n pushfq\n pop %1" : "+r"(r), "=r"(fl) : "S"(c) : "cc"); printf("adcw-imm8 %x c%d %x %03llx\n", a & 0xffff, c, r, FL(fl)); }
        { unsigned short r = a; u64 fl;
          __asm__ volatile("bt $0, %%esi\n" "sbbw $0x1234, %0\n pushfq\n pop %1" : "+r"(r), "=r"(fl) : "S"(c) : "cc"); printf("sbbw-imm16 %x c%d %x %03llx\n", a & 0xffff, c, r, FL(fl)); }
        { unsigned char r = a; int z;                                   /* the flags feed a branch */
          __asm__ volatile("bt $0, %%esi\n" "adcb %2, %0\n setc %%r8b\n movzbl %%r8b, %1" : "+q"(r), "=r"(z) : "q"((unsigned char) b), "S"(c) : "r8", "cc"); printf("adcb-c %d\n", z); }
    }
    for (unsigned i = 0; i < 10; i++) for (unsigned n = 0; n < 20; n += 3) {
        unsigned a = vs[i];
        { unsigned char r = a; __asm__ volatile("rolb %%cl, %0" : "+q"(r) : "c"(n) : "cc"); printf("rolb-cl %x %u %x\n", a & 255, n, r); }
        { unsigned char r = a; __asm__ volatile("rorb %%cl, %0" : "+q"(r) : "c"(n) : "cc"); printf("rorb-cl %x %u %x\n", a & 255, n, r); }
        { unsigned short r = a; __asm__ volatile("rolw %%cl, %0" : "+r"(r) : "c"(n) : "cc"); printf("rolw-cl %x %u %x\n", a & 0xffff, n, r); }
        { unsigned short r = a; __asm__ volatile("rorw %%cl, %0" : "+r"(r) : "c"(n) : "cc"); printf("rorw-cl %x %u %x\n", a & 0xffff, n, r); }
        { unsigned char r = a; __asm__ volatile("rolb $1, %0" : "+q"(r) :: "cc"); printf("rolb1 %x %x\n", a & 255, r); }
        { unsigned char r = a; __asm__ volatile("rorb $1, %0" : "+q"(r) :: "cc"); printf("rorb1 %x %x\n", a & 255, r); }
        { unsigned short r = a; __asm__ volatile("rorw $1, %0" : "+r"(r) :: "cc"); printf("rorw1 %x %x\n", a & 0xffff, r); }
        { unsigned short m = a; __asm__ volatile("rolw %%cl, %0" : "+m"(m) : "c"(n) : "cc"); printf("rolw-cl[m] %x %u %x\n", a & 0xffff, n, m); }
        { unsigned char r = a; u64 f; __asm__ volatile("rolb %%cl, %0\n pushfq\n pop %1" : "+q"(r), "=r"(f) : "c"(n) : "cc"); printf("rolb-cl-f %x %u %x cf=%llu\n", a & 255, n, r, n & 31 ? f & 1 : 9); }
        { unsigned short r = a; u64 f; __asm__ volatile("rorw %%cl, %0\n pushfq\n pop %1" : "+r"(r), "=r"(f) : "c"(n) : "cc"); printf("rorw-cl-f %x %u %x cf=%llu\n", a & 0xffff, n, r, n & 31 ? f & 1 : 9); }
        { unsigned char r = a; u64 f; __asm__ volatile("rorb $1, %0\n pushfq\n pop %1" : "+q"(r), "=r"(f) :: "cc"); printf("rorb1-f %x %x cf=%llu of=%llu\n", a & 255, r, f & 1, (f >> 11) & 1); }
        { unsigned short m = a; u64 f; __asm__ volatile("rolw $3, %0\n pushfq\n pop %1" : "+m"(m), "=r"(f) :: "cc"); printf("rolw3-f %x %x cf=%llu\n", a & 0xffff, m, f & 1); }
    }
    for (unsigned i = 0; i < 10; i++) for (unsigned k = 0; k < 10; k++) for (int cc = 0; cc < 2; cc++) {
        unsigned short d = 0x5555 + i, s = vs[k]; unsigned long long q = 0x1122334455667788ULL + i;
        __asm__ volatile("cmp $1, %2\n cmovb %1, %0" : "+r"(d) : "r"(s), "r"((unsigned)cc) : "cc"); printf("cmovw %x %x %d %x\n", i, s, cc, d);
        unsigned long long r = q; __asm__ volatile("cmp $1, %2\n cmovbw %w1, %w0" : "+r"(r) : "r"(s), "r"((unsigned)cc) : "cc"); printf("cmovw64 %llx\n", r);
        unsigned short m = vs[i]; unsigned short im = vs[k]; __asm__ volatile("imul %1, %0" : "+r"(m) : "r"(im) : "cc"); printf("imulw %x %x %x\n", vs[i], vs[k], m);
        { unsigned short bsr = 0x7777, v = vs[k]; __asm__ volatile("bsr %1, %0" : "+r"(bsr) : "r"(v) : "cc"); printf("bsrw %x %x\n", v, bsr); }
        { unsigned short bsf = 0x7777, v = vs[k]; __asm__ volatile("bsf %1, %0" : "+r"(bsf) : "r"(v) : "cc"); printf("bsfw %x %x\n", v, bsf); }
    }
    { unsigned long long x = 0x1234567890abcdefULL, y = 0xffffffff11223344ULL, r1, r2;
      __asm__ volatile("lea 0x10(%%eax,%%ecx,4), %0" : "=r"(r1) : "a"(x), "c"(y)); printf("lea32 %llx\n", r1);
      __asm__ volatile("lea 0x10(%%eax,%%ecx,4), %%edx" : "=d"(r2) : "a"(x), "c"(y)); printf("lea32d %llx\n", r2); }
}

static void bits(void) {
    static const u64 vs[] = { 0, 1, 0x80000000ULL, 0xffffffffULL, 0x123456789abcdef0ULL, 0x8000000000000000ULL, 0xfedcba9876543210ULL, 7 };
    for (int i = 0; i < 8; i++) for (int k = 0; k < 8; k++) for (unsigned n = 0; n < 70; n += 7) {
        u64 a = vs[i], b = vs[k], f; unsigned a32 = (unsigned) a, b32 = (unsigned) b;
        __asm__ volatile("shldq %%cl, %2, %0\n pushfq\n pop %1" : "+r"(a), "=r"(f) : "r"(b), "c"(n) : "cc"); printf("shldq-cl %llx %llx %u %llx %s\n", vs[i], vs[k], n, a, (n & 63) ? "" : "");
        a = vs[i]; __asm__ volatile("shrdq %%cl, %2, %0\n pushfq\n pop %1" : "+r"(a), "=r"(f) : "r"(b), "c"(n) : "cc"); printf("shrdq-cl %llx %u %llx cf=%llu zf=%llu sf=%llu\n", vs[k], n, a, (n & 63) ? f & 1 : 9, (n & 63) ? (f >> 6) & 1 : 9, (n & 63) ? (f >> 7) & 1 : 9);
        __asm__ volatile("shldl %%cl, %2, %0\n pushfq\n pop %1" : "+r"(a32), "=r"(f) : "r"(b32), "c"(n) : "cc"); printf("shldl-cl %x %u cf=%llu zf=%llu\n", a32, n, (n & 31) ? f & 1 : 9, (n & 31) ? (f >> 6) & 1 : 9);
        a32 = (unsigned) vs[i]; __asm__ volatile("shrdl %%cl, %2, %0" : "+r"(a32) : "r"(b32), "c"(n) : "cc"); printf("shrdl-cl-dead %x\n", a32);
        { u64 m = vs[i]; __asm__ volatile("shldq %%cl, %1, %0\n pushfq\n pop %2" : "+m"(m), "+r"(b), "=r"(f) : "c"(n) : "cc"); printf("shldq-m %llx cf=%llu\n", m, (n & 63) ? f & 1 : 9); }
    }
    for (int i = 0; i < 8; i++) for (int k = 0; k < 8; k++) {
        u64 a = vs[i], b = vs[k], f;
        __asm__ volatile("shldq $13, %2, %0\n pushfq\n pop %1" : "+r"(a), "=r"(f) : "r"(b) : "cc"); printf("shldq-13 %llx cf=%llu zf=%llu sf=%llu pf=%llu\n", a, f & 1, (f >> 6) & 1, (f >> 7) & 1, (f >> 2) & 1);
        a = vs[i]; __asm__ volatile("shrdq $1, %2, %0\n pushfq\n pop %1" : "+r"(a), "=r"(f) : "r"(b) : "cc"); printf("shrdq-1 %llx cf=%llu of=%llu zf=%llu\n", a, f & 1, (f >> 11) & 1, (f >> 6) & 1);
        unsigned a32 = (unsigned) vs[i], b32 = (unsigned) b; __asm__ volatile("shrdl $31, %2, %0\n pushfq\n pop %1" : "+r"(a32), "=r"(f) : "r"(b32) : "cc"); printf("shrdl-31 %x cf=%llu\n", a32, f & 1);
        a32 = (unsigned) vs[i]; __asm__ volatile("shldl $0, %1, %0" : "+r"(a32) : "r"(b32) : "cc"); printf("shldl-0 %x\n", a32);
        { u64 m = vs[i]; __asm__ volatile("shrdq $5, %1, %0" : "+m"(m) : "r"(b) : "cc"); printf("shrdq-5m %llx\n", m); }
    }
    /* lock bts / btr / btc */
    static u64 mem[4];
    for (int k = 0; k < 3; k++) for (long bit = -70; bit < 140; bit += 13) {
        mem[0] = 0x0123456789abcdefULL; mem[1] = 0xfedcba9876543210ULL; mem[2] = ~0ULL; mem[3] = 0x5555aaaa5555aaaaULL;
        u64 f; u64 *base = mem + 2; long bo = bit;
        if (k == 0) __asm__ volatile("lock btsq %2, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(base), "r"(bo) : "cc", "memory");
        else if (k == 1) __asm__ volatile("lock btrq %2, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(base), "r"(bo) : "cc", "memory");
        else __asm__ volatile("lock btcq %2, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(base), "r"(bo) : "cc", "memory");
        printf("lockbt%d %ld cf=%llu %llx %llx %llx %llx\n", k, bit, f & 1, mem[0], mem[1], mem[2], mem[3]);
        mem[0] = 0x0123456789abcdefULL; mem[1] = 0xfedcba9876543210ULL; mem[2] = ~0ULL; mem[3] = 0x5555aaaa5555aaaaULL;
        int b32 = (int) bit;
        if (k == 0) __asm__ volatile("lock btsl %2, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(base), "r"(b32) : "cc", "memory");
        else if (k == 1) __asm__ volatile("lock btrl %2, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(base), "r"(b32) : "cc", "memory");
        else __asm__ volatile("lock btcl %2, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(base), "r"(b32) : "cc", "memory");
        printf("lockbtl%d %d cf=%llu %llx %llx %llx %llx\n", k, b32, f & 1, mem[0], mem[1], mem[2], mem[3]);
    }
    for (int bit = 0; bit < 64; bit += 5) {
        mem[0] = 0x0123456789abcdefULL; mem[1] = 0xfedcba9876543210ULL; u64 f;
        switch (bit % 3) {
        case 0: __asm__ volatile("lock btsq $37, 8(%1)\n pushfq\n pop %0" : "=r"(f) : "r"(mem) : "cc", "memory"); break;
        case 1: __asm__ volatile("lock btrq $4, (%1)\n pushfq\n pop %0" : "=r"(f) : "r"(mem) : "cc", "memory"); break;
        default: __asm__ volatile("lock btcl $9, 4(%1)\n pushfq\n pop %0" : "=r"(f) : "r"(mem) : "cc", "memory");
        }
        printf("lockbti %d cf=%llu %llx %llx\n", bit, f & 1, mem[0], mem[1]);
        __asm__ volatile("lock btsw $3, (%0)" :: "r"(mem) : "cc", "memory"); printf("lockbtw %llx\n", mem[0]);
        __asm__ volatile("lock btsq $63, (%0)\n jnc 1f\n incq 8(%0)\n1:" :: "r"(mem) : "cc", "memory"); printf("lockbt-jc %llx %llx\n", mem[0], mem[1]);
    }
    /* mul / div 8 and 16 */
    static const unsigned xs[] = { 0, 1, 2, 3, 0x7f, 0x80, 0xff, 0x100, 0x1234, 0x7fff, 0x8000, 0xffff, 0xfffe };
    for (unsigned i = 0; i < 13; i++) for (unsigned k = 0; k < 13; k++) {
        unsigned ax = xs[i] & 0xffff, y = xs[k]; u64 f; unsigned short dx;
        __asm__ volatile("mulb %%cl\n pushfq\n pop %1" : "+a"(ax), "=r"(f) : "c"(y & 255) : "cc"); printf("mulb %x %x -> %x cf=%llu of=%llu\n", xs[i] & 0xffff, y & 255, ax & 0xffff, f & 1, (f >> 11) & 1);
        ax = xs[i] & 0xffff; __asm__ volatile("imulb %%cl\n pushfq\n pop %1" : "+a"(ax), "=r"(f) : "c"(y & 255) : "cc"); printf("imulb %x %x -> %x cf=%llu of=%llu\n", xs[i] & 0xffff, y & 255, ax & 0xffff, f & 1, (f >> 11) & 1);
        ax = xs[i] & 0xffff; dx = 0x55; __asm__ volatile("mulw %%cx\n" : "+a"(ax), "+d"(dx) : "c"((unsigned short) y) : "cc"); printf("mulw %x %x -> %x %x\n", xs[i] & 0xffff, y & 0xffff, ax & 0xffff, dx);
        ax = xs[i] & 0xffff; dx = 0x55; __asm__ volatile("imulw %%cx\n" : "+a"(ax), "+d"(dx) : "c"((unsigned short) y) : "cc"); printf("imulw %x %x -> %x %x\n", xs[i] & 0xffff, y & 0xffff, ax & 0xffff, dx);
        { unsigned char m = y; ax = xs[i] & 0xffff; __asm__ volatile("mulb %1" : "+a"(ax) : "m"(m) : "cc"); printf("mulb[m] %x\n", ax & 0xffff); }
        if (y & 255) { ax = xs[i] & 0xffff; if ((ax / (y & 255)) < 256) { __asm__ volatile("divb %%cl" : "+a"(ax) : "c"(y & 255) : "cc"); printf("divb %x %x -> %x\n", xs[i] & 0xffff, y & 255, ax & 0xffff); } }
        if (y & 255) { ax = xs[i] & 0xffff; int q = (short) ax / (signed char) y; if (q >= -128 && q <= 127 && !((signed char) y == -1 && (short) ax == -32768)) { __asm__ volatile("idivb %%cl" : "+a"(ax) : "c"(y & 255) : "cc"); printf("idivb %x %x -> %x\n", xs[i] & 0xffff, y & 255, ax & 0xffff); } }
        if (y & 0xffff) { ax = xs[i] & 0xffff; dx = (unsigned short) (k & 1); unsigned n = ((unsigned) dx << 16) | ax; if (n / (y & 0xffff) <= 0xffff) { __asm__ volatile("divw %%cx" : "+a"(ax), "+d"(dx) : "c"((unsigned short) y) : "cc"); printf("divw %x %x %x -> %x %x\n", xs[i] & 0xffff, k & 1, y & 0xffff, ax & 0xffff, dx); } }
        if (y & 0xffff) { ax = xs[i] & 0xffff; dx = (ax & 0x8000) ? 0xffff : 0; int n = (int) (((unsigned) dx << 16) | ax); short dv = (short) y; if (dv != 0) { long q = (long) n / dv; if (q >= -32768 && q <= 32767 && !(dv == -1 && n == -2147483647 - 1)) { __asm__ volatile("idivw %%cx" : "+a"(ax), "+d"(dx) : "c"((unsigned short) y) : "cc"); printf("idivw %x %x -> %x %x\n", xs[i] & 0xffff, y & 0xffff, ax & 0xffff, dx); } } }
    }
}
int main(void) {
    narrow(); bits();
    MOVS("b", 20, "cld"); MOVS("w", 9, "cld"); MOVS("l", 5, "cld"); MOVS("q", 3, "cld"); MOVS("q", 0, "cld");
    MOVS("b", 10, "std"); MOVS("l", 3, "std"); MOVS("q", 2, "std");
    MOVS1("b"); MOVS1("w"); MOVS1("l"); MOVS1("q");
    STOS("b", "a", 0x5a, 17, "cld"); STOS("w", "a", 0x1234, 7, "cld"); STOS("l", "a", 0xdeadbeef, 5, "cld"); STOS("q", "a", 0x1122334455667788ULL, 4, "cld");
    STOS("l", "a", 0x01010101, 6, "cld"); STOS("q", "a", 0, 3, "cld"); STOS("b", "a", 0x77, 9, "std"); STOS("q", "a", 0x99, 3, "std");
    cmps(); scas(); lods();
    unsigned ea, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(ea), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
    printf("~cpuid0 %u\n", ea >= 1);
    __asm__ volatile("cpuid" : "=a"(ea), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    printf("~cpuid1 sse2=%u\n", (edx >> 26) & 1);
    u64 t1, t2; unsigned lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); t1 = ((u64) hi << 32) | lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); t2 = ((u64) hi << 32) | lo;
    printf("rdtsc monotonic=%d\n", t2 >= t1);
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    printf("~xgetbv sse=%u\n", (lo >> 1) & 1);
    return 0;
}

/* tests/guests/jit_ro.c - memory operands of the form [base + index*scale] without a displacement (guest for tests/integration/jit_embed.sh).
 * The JIT turns them into one register-offset A64 load / store (j_ea_x, m_ld / m_st with JEA_RO) when the scale is 1 or the access size; any other scale, a displacement,
 * a segment or a 32-bit address keeps the add + access sequence. Every size (1/2/4/8), every scale (1/2/4/8), loads, stores, movzx / movsx, cmp / test / add / sub / and / or / xor
 * with the memory operand on either side, read-modify-write, and a compare + jcc over every unsigned condition, with the base and index in pinned registers.
 * Prints a hash per case; interpreter, JIT and native must agree. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t u64;
static u64 h;
static void mixv(u64 v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 255; h *= 1099511628211UL; } }
static unsigned long long rng = 88172645463325252ULL;
static u64 rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static unsigned char buf[4096] __attribute__((aligned(16)));
static void fill(void) { for (int i = 0; i < (int) sizeof buf; i++) buf[i] = (unsigned char) rnd(); }
static void sum(void) { for (int i = 0; i < (int) sizeof buf; i++) { h ^= buf[i]; h *= 1099511628211UL; } }

/* load: dst = [rsi + rdx*SC] */
#define LD(name, ins, sc, dst, mask) \
    static __attribute__((noinline)) u64 name(u64 base, u64 idx) { u64 r = 0x1122334455667788ULL; __asm__ volatile(ins " (%1,%2," #sc "), " dst : "+r"(r) : "r"(base), "r"(idx) : "memory"); return r & (mask); }
LD(ld8_1, "movzbl", 1, "%k0", 0xffffffffULL)
LD(ld8_2, "movzbl", 2, "%k0", 0xffffffffULL)
LD(ld8_4, "movzbl", 4, "%k0", 0xffffffffULL)
LD(ld8_8, "movzbl", 8, "%k0", 0xffffffffULL)
LD(ld16_1, "movzwl", 1, "%k0", 0xffffffffULL)
LD(ld16_2, "movzwl", 2, "%k0", 0xffffffffULL)
LD(ld16_4, "movzwl", 4, "%k0", 0xffffffffULL)
LD(ld32_1, "movl", 1, "%k0", 0xffffffffULL)
LD(ld32_2, "movl", 2, "%k0", 0xffffffffULL)
LD(ld32_4, "movl", 4, "%k0", 0xffffffffULL)
LD(ld32_8, "movl", 8, "%k0", 0xffffffffULL)
LD(ld64_1, "movq", 1, "%0", ~0ULL)
LD(ld64_2, "movq", 2, "%0", ~0ULL)
LD(ld64_4, "movq", 4, "%0", ~0ULL)
LD(ld64_8, "movq", 8, "%0", ~0ULL)
LD(sx8_32, "movsbl", 1, "%k0", 0xffffffffULL)
LD(sx8_64, "movsbq", 1, "%0", ~0ULL)
LD(sx8_64s, "movsbq", 4, "%0", ~0ULL)
LD(sx16_32, "movswl", 2, "%k0", 0xffffffffULL)
LD(sx16_64, "movswq", 2, "%0", ~0ULL)
LD(sx16_64b, "movswq", 1, "%0", ~0ULL)
LD(sx32_64, "movslq", 4, "%0", ~0ULL)
LD(sx32_64b, "movslq", 1, "%0", ~0ULL)
LD(zx8_64, "movzbq", 1, "%0", ~0ULL)
LD(zx16_64, "movzwq", 2, "%0", ~0ULL)
/* 8/16-bit loads keep the upper bits of the destination */
LD(m8_1, "movb", 1, "%b0", ~0ULL)
LD(m16_2, "movw", 2, "%w0", ~0ULL)

/* store: [rsi + rdx*SC] = src */
#define ST(name, ins, sc, srcreg, sz) \
    static __attribute__((noinline)) void name(u64 base, u64 idx, u64 v) { __asm__ volatile(ins " " srcreg ", (%0,%1," #sc ")" : : "r"(base), "r"(idx), "r"(v) : "memory"); }
ST(st8_1, "movb", 1, "%b2", 1)
ST(st8_4, "movb", 4, "%b2", 1)
ST(st16_2, "movw", 2, "%w2", 2)
ST(st16_1, "movw", 1, "%w2", 2)
ST(st32_4, "movl", 4, "%k2", 4)
ST(st32_1, "movl", 1, "%k2", 4)
ST(st64_8, "movq", 8, "%2", 8)
ST(st64_1, "movq", 1, "%2", 8)
static __attribute__((noinline)) void sti8(u64 base, u64 idx) { __asm__ volatile("movb $0x9c, (%0,%1,1)" : : "r"(base), "r"(idx) : "memory"); }
static __attribute__((noinline)) void sti16(u64 base, u64 idx) { __asm__ volatile("movw $0xbeef, (%0,%1,2)" : : "r"(base), "r"(idx) : "memory"); }
static __attribute__((noinline)) void sti32(u64 base, u64 idx) { __asm__ volatile("movl $0xdeadbeef, (%0,%1,4)" : : "r"(base), "r"(idx) : "memory"); }
static __attribute__((noinline)) void sti64(u64 base, u64 idx) { __asm__ volatile("movq $-5, (%0,%1,8)" : : "r"(base), "r"(idx) : "memory"); }

/* reg op [mem] and [mem] op reg, read-modify-write; the flags are printed through a setcc chain so they are live */
#define ALU(name, ins, sc, rsuf, rreg, memfirst) \
    static __attribute__((noinline)) u64 name(u64 base, u64 idx, u64 v) { u64 f; \
        __asm__ volatile(ins rsuf " " memfirst "\n\tpushfq\n\tpopq %1" : "+r"(v), "=r"(f) : "r"(base), "r"(idx) : "memory", "cc"); return (v << 12) ^ (f & 0x8c5); }
#define ALUM(name, ins, rsuf, sc, srcreg) \
    static __attribute__((noinline)) u64 name(u64 base, u64 idx, u64 v) { u64 f; \
        __asm__ volatile(ins rsuf " " srcreg ", (%2,%3," #sc ")\n\tpushfq\n\tpopq %1" : "+r"(v), "=r"(f) : "r"(base), "r"(idx) : "memory", "cc"); return (v << 12) ^ (f & 0x8c5); }
ALUM(addm32, "add", "l", 4, "%k0")
ALUM(addm64, "add", "q", 8, "%0")
ALUM(addm8, "add", "b", 1, "%b0")
ALUM(addm16, "add", "w", 2, "%w0")
ALUM(subm32, "sub", "l", 4, "%k0")
ALUM(andm32, "and", "l", 1, "%k0")
ALUM(orm64, "or", "q", 1, "%0")
ALUM(xorm16, "xor", "w", 1, "%w0")
ALUM(cmpm8, "cmp", "b", 1, "%b0")
ALUM(cmpm16, "cmp", "w", 2, "%w0")
ALUM(cmpm32, "cmp", "l", 4, "%k0")
ALUM(cmpm64, "cmp", "q", 8, "%0")
ALUM(testm8, "test", "b", 1, "%b0")
ALUM(testm32, "test", "l", 4, "%k0")
#define ALUR(name, ins, rsuf, sc, dst) \
    static __attribute__((noinline)) u64 name(u64 base, u64 idx, u64 v) { u64 f; \
        __asm__ volatile(ins rsuf " (%2,%3," #sc "), " dst "\n\tpushfq\n\tpopq %1" : "+r"(v), "=r"(f) : "r"(base), "r"(idx) : "memory", "cc"); return (v << 12) ^ (f & 0x8c5); }
ALUR(addr32, "add", "l", 4, "%k0")
ALUR(addr64, "add", "q", 8, "%0")
ALUR(subr64, "sub", "q", 1, "%0")
ALUR(andr32, "and", "l", 2, "%k0")
ALUR(xorr8, "xor", "b", 1, "%b0")
ALUR(cmpr8, "cmp", "b", 1, "%b0")
ALUR(cmpr16, "cmp", "w", 2, "%w0")
ALUR(cmpr32, "cmp", "l", 4, "%k0")
ALUR(cmpr64, "cmp", "q", 8, "%0")
static __attribute__((noinline)) u64 incm(u64 base, u64 idx) { __asm__ volatile("incl (%0,%1,4)" : : "r"(base), "r"(idx) : "memory", "cc"); return 0; }
static __attribute__((noinline)) u64 negm(u64 base, u64 idx) { __asm__ volatile("negq (%0,%1,8)" : : "r"(base), "r"(idx) : "memory", "cc"); return 0; }
static __attribute__((noinline)) u64 notm(u64 base, u64 idx) { __asm__ volatile("notw (%0,%1,2)" : : "r"(base), "r"(idx) : "memory", "cc"); return 0; }

/* compare + jcc over the unsigned conditions: a loop over the buffer that counts how often each condition holds */
#define JC(name, ld, cmpi, cc) \
    static __attribute__((noinline)) u64 name(u64 base, u64 n, u64 k) { u64 c = 0, i = 0; \
        __asm__ volatile("1:\n\t" cmpi " %b3, (%1,%2,1)\n\tj" cc " 2f\n\tjmp 3f\n2:\n\tinc %0\n3:\n\tinc %2\n\tcmp %4, %2\n\tjb 1b" : "+r"(c), "+r"(base), "+r"(i) : "q"(k), "r"(n) : "memory", "cc"); return c; }
JC(jc_e, 0, "cmpb", "e")
JC(jc_ne, 0, "cmpb", "ne")
JC(jc_b, 0, "cmpb", "b")
JC(jc_ae, 0, "cmpb", "ae")
JC(jc_be, 0, "cmpb", "be")
JC(jc_a, 0, "cmpb", "a")
JC(jc_l, 0, "cmpb", "l")
JC(jc_g, 0, "cmpb", "g")
#define JT(name, cc) \
    static __attribute__((noinline)) u64 name(u64 base, u64 n, u64 k) { u64 c = 0, i = 0; \
        __asm__ volatile("1:\n\t" "testb %b3, (%1,%2,1)\n\tj" cc " 2f\n\tjmp 3f\n2:\n\tinc %0\n3:\n\tinc %2\n\tcmp %4, %2\n\tjb 1b" : "+r"(c), "+r"(base), "+r"(i) : "q"(k), "r"(n) : "memory", "cc"); return c; }
JT(jt_e, "e")
JT(jt_ne, "ne")
JT(jt_s, "s")
JT(jt_a, "a")
#define JI8(name, cc) \
    static __attribute__((noinline)) u64 name(u64 base, u64 n, u64 k) { u64 c = 0, i = 0; (void) k; \
        __asm__ volatile("1:\n\t" "cmpb $0x80, (%1,%2,1)\n\tj" cc " 2f\n\tjmp 3f\n2:\n\tinc %0\n3:\n\tinc %2\n\tcmp %4, %2\n\tjb 1b" : "+r"(c), "+r"(base), "+r"(i) : "q"(k), "r"(n) : "memory", "cc"); return c; }
JI8(ji_e, "e")
JI8(ji_b, "b")
JI8(ji_a, "a")
JI8(ji_g, "g")
#define JI16(name, cc) \
    static __attribute__((noinline)) u64 name(u64 base, u64 n, u64 k) { u64 c = 0, i = 0; (void) k; \
        __asm__ volatile("1:\n\t" "cmpw $0x1234, (%1,%2,2)\n\tj" cc " 2f\n\tjmp 3f\n2:\n\tinc %0\n3:\n\tinc %2\n\tcmp %4, %2\n\tjb 1b" : "+r"(c), "+r"(base), "+r"(i) : "q"(k), "r"(n) : "memory", "cc"); return c; }
JI16(jw_e, "e")
JI16(jw_b, "b")
JI16(jw_a, "a")
JI16(jw_l, "l")

typedef u64 (*lf)(u64, u64);
int main(void) {
    static const struct { const char *n; lf f; int sc; } L[] = {
        { "ld8_1", ld8_1, 1 }, { "ld8_2", ld8_2, 2 }, { "ld8_4", ld8_4, 4 }, { "ld8_8", ld8_8, 8 }, { "ld16_1", ld16_1, 1 }, { "ld16_2", ld16_2, 2 }, { "ld16_4", ld16_4, 4 },
        { "ld32_1", ld32_1, 1 }, { "ld32_2", ld32_2, 2 }, { "ld32_4", ld32_4, 4 }, { "ld32_8", ld32_8, 8 }, { "ld64_1", ld64_1, 1 }, { "ld64_2", ld64_2, 2 }, { "ld64_4", ld64_4, 4 },
        { "ld64_8", ld64_8, 8 }, { "sx8_32", sx8_32, 1 }, { "sx8_64", sx8_64, 1 }, { "sx8_64s", sx8_64s, 4 }, { "sx16_32", sx16_32, 2 }, { "sx16_64", sx16_64, 2 }, { "sx16_64b", sx16_64b, 1 },
        { "sx32_64", sx32_64, 4 }, { "sx32_64b", sx32_64b, 1 }, { "zx8_64", zx8_64, 1 }, { "zx16_64", zx16_64, 2 }, { "m8_1", m8_1, 1 }, { "m16_2", m16_2, 2 },
    };
    fill();
    for (unsigned k = 0; k < sizeof L / sizeof L[0]; k++) {
        h = 1469598103934665603ULL;
        for (int i = 0; i < 200; i++) { u64 idx = rnd() % 400; mixv(L[k].f((u64) buf + 16, idx)); }
        printf("%s %016llx\n", L[k].n, (unsigned long long) h);
    }
    h = 1469598103934665603ULL; fill();
    for (int i = 0; i < 100; i++) { u64 v = rnd(); u64 ix = rnd() % 300; st8_1((u64) buf, ix, v); st8_4((u64) buf + 1024, ix, v); st16_2((u64) buf + 2048, ix, v); st16_1((u64) buf + 512, ix, v);
        st32_4((u64) buf + 1536, ix & 63, v); st32_1((u64) buf + 700, ix, v); st64_8((u64) buf + 2560, ix & 31, v); st64_1((u64) buf + 3000, ix, v); }
    sti8((u64) buf, 5); sti16((u64) buf, 9); sti32((u64) buf, 11); sti64((u64) buf, 13);
    sum(); printf("stores %016llx\n", (unsigned long long) h);
    static const struct { const char *n; u64 (*f)(u64, u64, u64); int sc; } A[] = {
        { "addm32", addm32, 4 }, { "addm64", addm64, 8 }, { "addm8", addm8, 1 }, { "addm16", addm16, 2 }, { "subm32", subm32, 4 }, { "andm32", andm32, 1 }, { "orm64", orm64, 1 }, { "xorm16", xorm16, 1 },
        { "cmpm8", cmpm8, 1 }, { "cmpm16", cmpm16, 2 }, { "cmpm32", cmpm32, 4 }, { "cmpm64", cmpm64, 8 }, { "testm8", testm8, 1 }, { "testm32", testm32, 4 },
        { "addr32", addr32, 4 }, { "addr64", addr64, 8 }, { "subr64", subr64, 1 }, { "andr32", andr32, 2 }, { "xorr8", xorr8, 1 }, { "cmpr8", cmpr8, 1 }, { "cmpr16", cmpr16, 2 },
        { "cmpr32", cmpr32, 4 }, { "cmpr64", cmpr64, 8 },
    };
    for (unsigned k = 0; k < sizeof A / sizeof A[0]; k++) {
        h = 1469598103934665603ULL; fill();
        for (int i = 0; i < 200; i++) { u64 v = rnd(); if (i & 1) v &= 0xff; mixv(A[k].f((u64) buf + 8, rnd() % 100, v)); }
        sum(); printf("%s %016llx\n", A[k].n, (unsigned long long) h);
    }
    h = 1469598103934665603ULL; fill();
    for (int i = 0; i < 100; i++) { incm((u64) buf, rnd() % 200); negm((u64) buf + 1024, rnd() % 100); notm((u64) buf + 2048, rnd() % 200); }
    sum(); printf("rmw %016llx\n", (unsigned long long) h);
    static const struct { const char *n; u64 (*f)(u64, u64, u64); } J[] = {
        { "jc_e", jc_e }, { "jc_ne", jc_ne }, { "jc_b", jc_b }, { "jc_ae", jc_ae }, { "jc_be", jc_be }, { "jc_a", jc_a }, { "jc_l", jc_l }, { "jc_g", jc_g },
        { "jt_e", jt_e }, { "jt_ne", jt_ne }, { "jt_s", jt_s }, { "jt_a", jt_a }, { "ji_e", ji_e }, { "ji_b", ji_b }, { "ji_a", ji_a }, { "ji_g", ji_g },
        { "jw_e", jw_e }, { "jw_b", jw_b }, { "jw_a", jw_a }, { "jw_l", jw_l },
    };
    /* few distinct byte values so that every condition is taken sometimes */
    for (int i = 0; i < (int) sizeof buf; i++) buf[i] = (unsigned char) (rnd() % 5 == 0 ? 0x80 : rnd() % 4 == 0 ? 0x12 : 0x34 + (rnd() & 1) * 0x7e);
    for (unsigned k = 0; k < sizeof J / sizeof J[0]; k++) {
        h = 1469598103934665603ULL;
        for (int kv = 0; kv < 6; kv++) { static const unsigned char K[] = { 0x00, 0x34, 0x12, 0x80, 0xb2, 0xff }; mixv(J[k].f((u64) buf, 1000, K[kv])); }
        printf("%s %016llx\n", J[k].n, (unsigned long long) h);
    }
    return 0;
}

/* tests/guests/jit_ext.c - guest for tests/integration/jit_embed.sh: instructions that the JIT used to hand to the interpreter and now translates:
 * cbw / cwd (66 98 / 66 99), imul r16, r/m16, imm (66 69 / 66 6B) and pushfq / popfq (9C / 9D). SPDX-License-Identifier: Apache-2.0
 * Lines starting with '~' hold flags that the SDM calls undefined after imul (SF/ZF/AF/PF): the interpreter and the JIT must agree on them, a real CPU may not. */
#include <stdio.h>
#include <stdint.h>
static uint64_t seed = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static uint64_t val(void) {
    static const uint64_t e[] = { 0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, ~0ULL, 0x12345678ULL, 0xdeadbeefcafef00dULL, 0x00ff00ffULL, 0xff80ULL, 0x7f80ULL };
    uint64_t v = rnd(); return (rnd() % 3 == 0) ? e[rnd() % (sizeof e / sizeof e[0])] : (rnd() % 2 ? v : v >> (rnd() % 64));
}
static uint64_t H[4];
static void mix(int h, uint64_t v) { H[h] = (H[h] ^ v) * 0x100000001b3ULL; H[h] ^= H[h] >> 29; }
static void start(void) { for (int i = 0; i < 4; i++) H[i] = 14695981039346656037ULL; }
static void emit(const char *nm) { printf("%-16s %016llx\n~%-15s %016llx\n", nm, (unsigned long long) H[0], nm, (unsigned long long) H[1]); }
#define PRE "movl $0x7fffffff, %%eax; addl $1, %%eax; "          /* sets SF OF AF PF and clears ZF CF: the flags imul must keep or replace */
#define FL "pushfq; popq %1"
static __attribute__((aligned(16))) uint16_t M16;
int main(void) {
    /* cbw: AL -> AX, the rest of rax stays; cwd: sign of AX -> DX, the rest of rdx stays; cwde / cdqe / cdq as neighbours */
    start(); for (int i = 0; i < 3000; i++) { uint64_t a = val(), d = val(); uint64_t b = a;
        __asm__ volatile("cbtw" : "+a"(b)); mix(0, b);
        uint64_t x = a, y = d; __asm__ volatile("cwtd" : "+a"(x), "+d"(y)); mix(0, x); mix(0, y);
        uint64_t p = a, q = d; __asm__ volatile("cwtl" : "+a"(p)); __asm__ volatile("cltq" : "+a"(q)); mix(0, p); mix(0, q);
        uint64_t r = a, s = d; __asm__ volatile("cltd" : "+a"(r), "+d"(s)); mix(0, r); mix(0, s); }
    emit("cbw cwd cwde");
    /* imul r16 with register / memory source, flags used afterwards */
    start(); for (int i = 0; i < 2000; i++) { uint16_t a = (uint16_t) val(), b = (uint16_t) val(); unsigned long f;
        __asm__ volatile(PRE "imulw %2, %0; " FL : "+r"(a), "=r"(f) : "r"(b) : "cc", "rax"); mix(0, a); mix(0, f & 0x801); mix(1, f & 0x8d5); }
    emit("imul r16,r16");
    start(); for (int i = 0; i < 2000; i++) { uint16_t a = (uint16_t) val(); unsigned long f; M16 = (uint16_t) val();
        __asm__ volatile(PRE "imulw %2, %0; " FL : "+r"(a), "=r"(f) : "m"(M16) : "cc", "rax"); mix(0, a); mix(0, f & 0x801); mix(1, f & 0x8d5); }
    emit("imul r16,m16");
    /* the three-operand forms that were interpreted: imm8 and imm16, register and memory source */
#define IMUL3(nm, ins, imm) do { start(); for (int i = 0; i < 2000; i++) { uint16_t s = (uint16_t) val(), d; unsigned long f; \
    __asm__ volatile(PRE ins " $" #imm ", %2, %0; " FL : "=r"(d), "=r"(f) : "r"(s) : "cc", "rax"); mix(0, d); mix(0, f & 0x801); mix(1, f & 0x8d5); } emit(nm); } while (0)
    IMUL3("imul16 imm8 7", "imulw", 7); IMUL3("imul16 imm8 -3", "imulw", -3); IMUL3("imul16 imm16", "imulw", 1000); IMUL3("imul16 imm16 -", "imulw", -300); IMUL3("imul16 imm16 big", "imulw", 0x7fff);
#define IMUL3M(nm, imm) do { start(); for (int i = 0; i < 2000; i++) { uint16_t d; unsigned long f; M16 = (uint16_t) val(); \
    __asm__ volatile(PRE "imulw $" #imm ", %2, %0; " FL : "=r"(d), "=r"(f) : "m"(M16) : "cc", "rax"); mix(0, d); mix(0, f & 0x801); mix(1, f & 0x8d5); } emit(nm); } while (0)
    IMUL3M("imul16 m imm8", 11); IMUL3M("imul16 m imm16", 5000);
    /* conditions after imul r16, imm16 (o / c and the jumps that read them) */
    start(); for (int i = 0; i < 2000; i++) { uint16_t s = (uint16_t) val(), d; unsigned char c[4]; int jc = 0;
        __asm__ volatile("imulw $1234, %4, %0; seto %1; setc %2; setno %3" : "=r"(d), "=m"(c[0]), "=m"(c[1]), "=m"(c[2]) : "r"(s) : "cc"); mix(0, c[0] | c[1] << 1 | c[2] << 2); mix(0, d);
        __asm__ volatile("imulw $-77, %2, %1; jo 1f; addl $1, %0; 1: jc 2f; addl $2, %0; 2:" : "+r"(jc), "=r"(d) : "r"(s) : "cc"); mix(0, (uint64_t) jc + d); }
    emit("imul16 cc");
    /* flags dead afterwards */
    start(); for (int i = 0; i < 2000; i++) { uint16_t s = (uint16_t) val(), d; uint32_t z; __asm__ volatile("imulw $9, %2, %0; xorl %1, %1" : "=r"(d), "=r"(z) : "r"(s) : "cc"); mix(0, d + z); } emit("imul16 dead");
    /* popfq loads CF PF AF ZF SF DF OF; every condition must see the new flags, DF must come back through pushfq */
    start(); for (int i = 0; i < 3000; i++) { uint64_t w = (val() & 0x8d5) | (rnd() % 2 ? 0x400 : 0) | 2, o; unsigned char c[8];
        __asm__ volatile("pushq %9; popfq; sete %1; setb %2; sets %3; seto %4; setp %5; setbe %6; setl %7; setle %8; pushfq; popq %0; cld"
                         : "=r"(o), "=m"(c[0]), "=m"(c[1]), "=m"(c[2]), "=m"(c[3]), "=m"(c[4]), "=m"(c[5]), "=m"(c[6]), "=m"(c[7]) : "r"(w) : "cc", "memory");
        mix(0, o & 0xcd5); for (int k = 0; k < 8; k++) mix(0, c[k] + (uint64_t) k * 3); }
    emit("popf pushf");
    /* pushfq right after arithmetic (lazy flags) and popfq followed by a jump */
    start(); for (int i = 0; i < 3000; i++) { uint64_t a = val(), b = val(), o; int j = 0;
        __asm__ volatile("addq %2, %1; pushfq; popq %0" : "=r"(o), "+r"(a) : "r"(b) : "cc"); mix(0, o & 0x8d5); mix(0, a);
        uint64_t w = (val() & 0x8d5) | 2; __asm__ volatile("pushq %2; popfq; jz 1f; addl $1, %0; 1: jc 2f; addl $2, %0; 2: js 3f; addl $4, %0; 3: jo 4f; addl $8, %0; 4:" : "+r"(j), "=m"(a) : "r"(w) : "cc", "memory"); mix(0, (uint64_t) j); }
    emit("popf jcc");
    return 0;
}

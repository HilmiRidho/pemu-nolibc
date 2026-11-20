/* tests/guests/jit_muldiv.c - guest for tests/integration/jit_embed.sh: one-operand mul / imul / div / idiv r/m32|64 with the flags used afterwards (CF/OF of mul, flags kept by div),
 * register and memory operands, wide dividends, divisor 0 and -1 (the slow path of the translation). A #DE is caught with a SIGFPE handler. SPDX-License-Identifier: Apache-2.0
 * Lines starting with '~' hold flags that the SDM calls undefined (SF ZF AF PF after mul / imul, all after div). */
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
static uint64_t seed = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static uint64_t val(void) {
    static const uint64_t e[] = { 0, 1, 2, ~0ULL, ~0ULL - 1, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 0x100000000ULL };
    return (rnd() % 3 == 0) ? e[rnd() % (sizeof e / sizeof e[0])] : (rnd() % 2 ? rnd() : rnd() >> (rnd() % 64));
}
static uint64_t H[2];
static void mix(int h, uint64_t v) { H[h] = (H[h] ^ v) * 0x100000001b3ULL; H[h] ^= H[h] >> 29; }
static void start(void) { H[0] = H[1] = 14695981039346656037ULL; }
static void emit(const char *nm) { printf("%-14s %016llx\n~%-13s %016llx\n", nm, (unsigned long long) H[0], nm, (unsigned long long) H[1]); }
static sigjmp_buf jb; static int trapped;
static void on_fpe(int s) { (void) s; trapped++; siglongjmp(jb, 1); }
#define PRE "movl $0x7fffffff, %%ecx; addl $1, %%ecx; "
static __attribute__((aligned(16))) uint64_t M;
#define CASE(nm, T, ins, DIV, MEM) do { start(); trapped = 0; for (int i = 0; i < 1500; i++) { T a = (T) val(), d = (T) val(), b = (T) val(); unsigned long f = 0; \
    if (DIV && i % 2) d = (T)(d >> (sizeof(T) * 4)); if (DIV && i % 7 == 0) b = 0; if (DIV && i % 11 == 0) b = (T) -1; M = b; \
    if (sigsetjmp(jb, 1) == 0) { __asm__ volatile(PRE ins " ; pushfq; popq %2" : "+a"(a), "+d"(d), "=r"(f) : "r"(b), "m"(M) : "cc", "rcx"); } \
    mix(0, (uint64_t) a); mix(0, (uint64_t) d); mix(0, f & 0x801); mix(1, f & 0x8d5); (void) MEM; } mix(0, (uint64_t) trapped); emit(nm); } while (0)
int main(void) {
    signal(SIGFPE, on_fpe);
    CASE("mul r32",  uint32_t, "mull %3", 0, 0);   CASE("mul r64",  uint64_t, "mulq %3", 0, 0);
    CASE("imul r32", uint32_t, "imull %3", 0, 0);  CASE("imul r64", uint64_t, "imulq %3", 0, 0);
    CASE("mul m32",  uint32_t, "mull %4", 0, 1);   CASE("mul m64",  uint64_t, "mulq %4", 0, 1);
    CASE("imul m32", uint32_t, "imull %4", 0, 1);  CASE("imul m64", uint64_t, "imulq %4", 0, 1);
    CASE("div r32",  uint32_t, "divl %3", 1, 0);   CASE("div r64",  uint64_t, "divq %3", 1, 0);
    CASE("idiv r32", uint32_t, "idivl %3", 1, 0);  CASE("idiv r64", uint64_t, "idivq %3", 1, 0);
    CASE("div m32",  uint32_t, "divl %4", 1, 1);   CASE("div m64",  uint64_t, "divq %4", 1, 1);
    CASE("idiv m32", uint32_t, "idivl %4", 1, 1);  CASE("idiv m64", uint64_t, "idivq %4", 1, 1);
    return 0;
}

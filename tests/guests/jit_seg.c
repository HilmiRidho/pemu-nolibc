/* tests/guests/jit_seg.c - pop r/m64, mov r/m16,Sreg and mov Sreg,r/m16 (guest for tests/integration/jit_embed.sh).
 * These three are on the hot path of Wine's system call dispatcher (pop into the frame, cs / ss stores, %fs reload), so they must not fall back to the interpreter.
 * Prints a hash per case; native, interpreter and JIT must agree. Build with -mno-red-zone (push / pop under the compiler's feet). SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <ucontext.h>
#include <sys/mman.h>
typedef uint64_t u64; typedef uint16_t u16;
static u64 h = 1469598103934665603ULL;
static void mix(u64 v) { h ^= v; h *= 1099511628211ULL; }
static unsigned long long rng = 88172645463325252ULL;
static u64 rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static u64 buf[64];

/* pop r/m64, memory forms: several bases / index / displacement sizes / a segment override. Returns the change of rsp across the pop (must be +8). */
#define POPM(name, ASM) static __attribute__((noinline)) u64 name(u64 v, u64 *b, u64 i) { u64 before, after; \
    __asm__ volatile("push %[v]\n\t mov %%rsp, %[b]\n\t " ASM "\n\t mov %%rsp, %[a]" : [b] "=&r"(before), [a] "=&r"(after) : [v] "r"(v), "a"(b), "c"(i) : "memory"); \
    return after - before; }
POPM(pm_base,   "pop (%%rax)")
POPM(pm_d8,     "pop 16(%%rax)")
POPM(pm_d32,    "pop 0x120(%%rax)")
POPM(pm_idx,    "pop (%%rax,%%rcx,8)")
POPM(pm_idxd8,  "pop -8(%%rax,%%rcx,8)")
POPM(pm_idxd32, "pop 0x100(%%rax,%%rcx,4)")
POPM(pm_gs,     "pop %%gs:(%%rax)")

/* pop r/m64, register forms written as 8F /0 (the assembler would pick 58+r) */
static __attribute__((noinline)) u64 pr_rcx(u64 v) { u64 r; __asm__ volatile("push %[v]\n\t .byte 0x8f, 0xc1" : "=c"(r) : [v] "r"(v) : "memory"); return r; }
static __attribute__((noinline)) u64 pr_r9(u64 v)  { register u64 r asm("r9"); __asm__ volatile("push %[v]\n\t .byte 0x41, 0x8f, 0xc1" : "=r"(r) : [v] "r"(v) : "memory"); return r; }

/* the address uses rsp: it is formed after rsp has moved up by 8 (the translator leaves this one to the interpreter) */
static __attribute__((noinline)) void pop_rsp(u64 *o) {
    __asm__ volatile("sub $32, %%rsp\n\t movq $1, (%%rsp)\n\t movq $2, 8(%%rsp)\n\t movq $3, 16(%%rsp)\n\t movq $4, 24(%%rsp)\n\t"
                     "pop 8(%%rsp)\n\t"
                     "mov -8(%%rsp), %%rax\n\t mov %%rax, (%0)\n\t mov (%%rsp), %%rax\n\t mov %%rax, 8(%0)\n\t mov 8(%%rsp), %%rax\n\t mov %%rax, 16(%0)\n\t mov 16(%%rsp), %%rax\n\t mov %%rax, 24(%0)\n\t"
                     "add $24, %%rsp" : : "r"(o) : "rax", "memory");
}

/* mov r/m16, Sreg: 16 / 32 / 64-bit register destinations (the upper bits: 16 bit keeps them, 32 / 64 zero-extend) and a memory destination */
#define SEGR(seg) static __attribute__((noinline)) void sr_##seg(u64 *o) { u64 a = 0xfedcba9876543210ULL, b = a, c = a; u16 m[2] = { 0x5a5a, 0x5a5a }; \
    __asm__ volatile("mov %%" #seg ", %%ax" : "+a"(a)); __asm__ volatile("mov %%" #seg ", %%ebx" : "+b"(b)); __asm__ volatile("mov %%" #seg ", %%rcx" : "+c"(c)); \
    __asm__ volatile("mov %%" #seg ", %0" : "=m"(m[0])); __asm__ volatile("mov %%" #seg ", 2(%0)" : : "r"(m) : "memory"); \
    o[0] = a; o[1] = b; o[2] = c; o[3] = m[0] | (u64) m[1] << 16; }
SEGR(cs) SEGR(ss) SEGR(ds) SEGR(es) SEGR(fs) SEGR(gs)

/* mov Sreg, r/m16: ignored, but a memory source is read. Only ds / es / gs with 0, which a real CPU accepts without touching anything the program uses. */
static __attribute__((noinline)) u64 ld_seg(void) {
    u16 z = 0; u64 x = 0x1234, y = 0;
    __asm__ volatile("mov %[z], %%ds\n\t mov %[z], %%es\n\t mov %[z], %%gs\n\t"
                     "xor %%eax, %%eax\n\t mov %%ax, %%ds\n\t mov %%ax, %%es\n\t mov %%eax, %%gs\n\t mov %[x], %[y]" : [y] "=r"(y) : [z] "m"(z), [x] "r"(x) : "rax", "memory");
    return y;
}

static sigjmp_buf jb; static volatile u64 f_addr, f_rsp, s_rsp; static volatile int f_sig;
static void on_segv(int s, siginfo_t *si, void *uc_) { ucontext_t *uc = uc_; f_sig = s; f_addr = (u64) si->si_addr; f_rsp = (u64) uc->uc_mcontext.gregs[REG_RSP]; siglongjmp(jb, 1); }
static void *bad;
static void fault_pop(void) {
    if (sigsetjmp(jb, 1) == 0) { __asm__ volatile("push $0x77\n\t mov %%rsp, %0\n\t pop (%%rax)" : "=m"(s_rsp) : "a"(bad) : "memory"); }
    printf("fault pop:  sig=%d addr=%s rsp_kept=%d\n", f_sig, f_addr == (u64) bad ? "bad" : "other", f_rsp == s_rsp);
}
static void fault_ldseg(void) {
    f_sig = 0;
    if (sigsetjmp(jb, 1) == 0) { __asm__ volatile("mov (%%rax), %%es" : : "a"(bad) : "memory"); }
    printf("fault mov es: sig=%d addr=%s\n", f_sig, f_addr == (u64) bad ? "bad" : "other");
}

int main(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigaction(SIGSEGV, &sa, NULL);
    bad = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for (int it = 0; it < 200; it++) {
        u64 v = rnd(), i = 8 + (rnd() & 7); memset(buf, 0, sizeof buf);
        u64 d, r = 0;
        r += pm_base(v, buf + 4, 0);          mix(buf[4]);
        r += pm_d8(v ^ 1, buf + 4, 0);        mix(buf[6]);
        r += pm_d32(v ^ 2, buf + 4, 0);       mix(buf[4 + 0x120 / 8]);
        r += pm_idx(v ^ 3, buf, i);           mix(buf[i]);
        r += pm_idxd8(v ^ 4, buf, i);         mix(buf[i - 1]);
        r += pm_idxd32(v ^ 5, buf, i);        mix(buf[i + 0x100 / 8]);
        r += pm_gs(v ^ 6, buf + 1, 0);        mix(buf[1]);
        mix(r);
        mix(pr_rcx(v ^ 7)); mix(pr_r9(v ^ 8));
        if (it % 50 == 49) printf("pop: round %d %016llx rsp_delta_sum=%llu\n", it, (unsigned long long) h, (unsigned long long) r);
    }
    { u64 o[4]; pop_rsp(o); printf("pop [rsp+8]: %llu %llu %llu %llu\n", (unsigned long long) o[0], (unsigned long long) o[1], (unsigned long long) o[2], (unsigned long long) o[3]); }
    { u64 o[4];
#define SHOW(seg) sr_##seg(o); printf("mov %s: %016llx %016llx %016llx %08llx\n", #seg, (unsigned long long) o[0], (unsigned long long) o[1], (unsigned long long) o[2], (unsigned long long) o[3]);
      SHOW(cs) SHOW(ss) SHOW(ds) SHOW(es) SHOW(fs) SHOW(gs) }
    printf("ld seg: %llx\n", (unsigned long long) ld_seg());
    fault_pop(); fault_ldseg();
    printf("done %016llx\n", (unsigned long long) h);
    return 0;
}

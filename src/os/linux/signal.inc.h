/* src/os/linux/signal.inc.h - signal state, host signal handlers, pending-signal delivery; shared by every guest. SPDX-License-Identifier: Apache-2.0
 * Needs from guest/<arch>/linux/guest.inc.h: GUEST_PC(cpu); and deliver_sig() (the guest's signal frame, guest/<arch>/linux/sigframe.inc.h). */
static void deliver_sig(CPU*c, int s);
struct gsa { u64 handler, flags, restorer, mask; };
static struct gsa gsa[65];
static __thread siginfo_t pinfo[65];
/* sigaltstack state of the calling guest thread (a new thread starts without one) */
static __thread u64 g_ss_sp, g_ss_size;
static __thread sigjmp_buf g_jb;
static __thread volatile int g_fault; static __thread volatile u64 g_faddr;
static __thread volatile int g_in_frame;               /* deliver_sig is writing the signal frame: a fault there is fatal (Linux: force_sigsegv), it must not be delivered again */
static __thread volatile int g_fwrite;                /* the faulting access was a store (x86 page-fault error code bit 1) */
#if defined(__aarch64__)
#include <asm/sigcontext.h>
#endif
static int fault_was_write(void*u) {
#if defined(__aarch64__) && defined(ESR_MAGIC)
    ucontext_t*uc = u; u8*r = (u8*) uc->uc_mcontext.__reserved;
    for (int n = 0; n < 16; n++) {                        /* walk the reserved records until the ESR one */
        struct _aarch64_ctx*h = (struct _aarch64_ctx*) r;
        if (!h->magic || !h->size) break;
        if (h->magic == ESR_MAGIC) return (int)((((struct esr_context*) h)->esr >> 6) & 1);   /* data abort: WnR */
        r += h->size;
    }
    return 0;
#elif defined(__x86_64__)
    return (int)((((ucontext_t*) u)->uc_mcontext.gregs[REG_ERR] >> 1) & 1);
#else
    (void) u; return 0;
#endif
}

/* Android (and containers) kill a process with SIGSYS when it calls a syscall its seccomp filter does not allow.
 * The emulator forwards guest syscalls to the host, so turn such a kill into -ENOSYS: guests (glibc) then fall back. */
static void hsys(int s, siginfo_t*si, void*u) {
    ucontext_t*uc = u; (void) s;
#ifdef si_syscall
    if (si) plog_ev("host-seccomp", (unsigned long long) si->si_syscall + 1, "the host's seccomp filter blocked host syscall %d (forwarded for the guest), answered ENOSYS", (int) si->si_syscall);
#else
    plog_ev("host-seccomp", 0, "the host's seccomp filter blocked a forwarded syscall, answered ENOSYS");
#endif
#ifdef si_syscall
    if (g_trace && si) pemu_tr("pemu: host seccomp blocked syscall %d -> ENOSYS\n", (int) si->si_syscall);
#else
    (void) si;
    if (g_trace) pemu_tr("pemu: host seccomp blocked a syscall -> ENOSYS\n");
#endif
#if defined(__aarch64__)
    uc->uc_mcontext.regs[0] = (u64)(s64) -ENOSYS;
#elif defined(__x86_64__)
    uc->uc_mcontext.gregs[REG_RAX] = -ENOSYS;
#else
    (void) uc;
#endif
}
static void hsig(int s, siginfo_t*si, void*u) { (void) u; if (si) pinfo[s] = *si; __atomic_or_fetch(PENDP(), 1ULL << s, __ATOMIC_SEQ_CST); }
static void hsegv(int s, siginfo_t*si, void*u) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_fault(u, g_cur);                                     /* fault inside translated code: recover the guest rip */
#else
    (void) u;
#endif
    if (g_in_frame) {                                                      /* no room for the frame (stack overflow, bad sigaltstack): the kernel kills the process with SIGSEGV; delivering again would loop for ever */
        g_in_frame = 0;
        plog_ev("guest-fault", 0, "cannot build the signal frame (guest stack not writable): process killed by SIGSEGV");
        plog_summary(); signal(SIGSEGV, SIG_DFL); host_raise(SIGSEGV); host_exit(128 + SIGSEGV);
    }
    if (gsa[s].handler > 1) { g_fwrite = fault_was_write(u); g_fault = s; g_faddr = (u64) si->si_addr; pinfo[s] = *si; siglongjmp(g_jb, 1); }
    plog_ev("guest-fault", 0, "fatal signal %d (%s) at addr=%p rip=%#llx, no guest handler", s, strsignal(s), si->si_addr, (unsigned long long) GUEST_PC(g_cur));
    plog_summary();
    pemu_tr("pemu: guest fault (signal %d) at addr=%p rip=%#llx\n", s, si->si_addr, (unsigned long long) GUEST_PC(g_cur));
    signal(s, SIG_DFL); host_raise(s);
    host_exit(128 + s);
}
static void trace_map(const char *tag, u64 a) {                  /* diagnostics: the mapping that holds address a, with the offset in its file */
    FILE *f = fopen("/proc/self/maps", "r"); char l[512];
    if (!f) return;
    while (fgets(l, sizeof l, f)) {
        unsigned long long lo, hi, off;
        if (sscanf(l, "%llx-%llx %*s %llx", &lo, &hi, &off) == 3 && a >= lo && a < hi) {
            l[strcspn(l, "\n")] = 0;
            pemu_tr("pemu:   %s %#llx = file offset %#llx in: %s\n", tag, (unsigned long long) a, off + (a - lo), l); break;
        }
    }
    fclose(f);
}
static void check_pending(CPU*c) {
    volatile u64 *pp = PENDP();
    while (*pp) {
        u64 p = *pp; int s = __builtin_ctzll(p);
        __atomic_and_fetch(pp, ~(1ULL << s), __ATOMIC_SEQ_CST);
        if (gsa[s].handler > 1) deliver_sig(c, s);
    }
}


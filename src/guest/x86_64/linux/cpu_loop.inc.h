/* src/guest/x86_64/linux/cpu_loop.inc.h - run loop. SPDX-License-Identifier: Apache-2.0 */
/* ---- hook-up of the Linux layer to the CPU core (PemuOS in src/guest/x86_64/cpu/cpu.inc.h) */
#include "sysnames.inc.h"
static void linux_exception(CPU *c, int excp) {
    switch (excp) {
    case EXCP_DE: host_raise(SIGFPE); break;
    case EXCP_BP: host_raise(SIGTRAP); break;
    case EXCP_MF: {                                    /* x87 exception pending: Linux sends SIGFPE with the si_code of the first unmasked flag (invalid, divide, overflow, underflow/denormal, inexact) */
        unsigned err = c->fsw & 0x3f & ~c->fcw;
        int code = (err & 1) ? 7 /*FPE_FLTINV*/ : (err & 4) ? 3 /*FPE_FLTDIV*/ : (err & 8) ? 4 /*FPE_FLTOVF*/ : (err & 0x12) ? 5 /*FPE_FLTUND*/ : 6 /*FPE_FLTRES*/;
        if (gsa[SIGFPE].handler > 1) {
            memset(&pinfo[SIGFPE], 0, sizeof pinfo[SIGFPE]);
            pinfo[SIGFPE].si_signo = SIGFPE; pinfo[SIGFPE].si_code = code; pinfo[SIGFPE].si_addr = (void*) c->rip;
            g_fault = 0; g_mf = 1;
            deliver_sig(c, SIGFPE);
            return;
        }
        plog_ev("guest-fault", 0, "rip=%#llx x87 exception with no guest SIGFPE handler: process killed (fcw=%#x fsw=%#x)", (unsigned long long) c->rip, c->fcw, c->fsw);
        plog_summary(); signal(SIGFPE, SIG_DFL); host_raise(SIGFPE); host_exit(128 + SIGFPE); }
    case EXCP_UD: plog_summary(); signal(SIGILL, SIG_DFL); host_raise(SIGILL); host_exit(132);        /* the instruction was logged by undef() */
    case EXCP_GP:                                      /* in/out/cli/sti/hlt: a real CPU raises #GP and Linux delivers SIGSEGV (si_code SI_KERNEL) */
        plog_ev("guest-fault", 0, "rip=%#llx privileged instruction (in/out/cli/sti/hlt) -> SIGSEGV%s", (unsigned long long) c->rip, gsa[SIGSEGV].handler > 1 ? " to the guest handler" : ", process killed");
        if (gsa[SIGSEGV].handler > 1) {
            memset(&pinfo[SIGSEGV], 0, sizeof pinfo[SIGSEGV]);
            pinfo[SIGSEGV].si_signo = SIGSEGV; pinfo[SIGSEGV].si_code = 0x80;
            g_fault = 0;
            deliver_sig(c, SIGSEGV);
            return;
        }
        plog_summary(); signal(SIGSEGV, SIG_DFL); host_raise(SIGSEGV); host_exit(128 + SIGSEGV);
    }
}
static void linux_negative(const char *kind, u64 key, const char *msg) { plog_core_cb(kind, key, msg); }
static const PemuOS linux_os = { do_syscall, linux_exception, linux_negative };
__attribute__((constructor)) static void linux_os_register(void) { g_os = &linux_os; plog_set_names(pemu_sysnames, PEMU_NSYSNAMES); }      /* before main(), so self-test builds that rename main() work too */


static void run(CPU*c) {
#ifdef HAVE_JIT
    if (g_jit_on) { jit_thread_init(); jit_rsb_clear(c); }
#endif
    if (sigsetjmp(g_jb, 1)) {
        int s = g_fault; sigset_t ss; sigemptyset(&ss); sigaddset(&ss, s); sigprocmask(SIG_UNBLOCK, &ss, NULL);
#ifdef HAVE_JIT
        c->patch = 0; jit_region_abort();
#endif
        deliver_sig(c, s);
    }
    for (;;) {
#ifdef HAVE_JIT
        if (g_jit_on && t_jt.ic) { if (*PENDP()) { c->patch = 0; check_pending(c); } jit_dispatch(c); continue; }
#endif
        if (*PENDP()) check_pending(c);
        step(c);
    }
}


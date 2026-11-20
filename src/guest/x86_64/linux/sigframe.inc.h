/* src/guest/x86_64/linux/sigframe.inc.h - x86_64 signal frame: building it (deliver_sig) and undoing it (rt_sigreturn). SPDX-License-Identifier: Apache-2.0 */
static __thread int g_mf;                                   /* the SIGFPE being delivered is an x87 #MF (vector 16), not #DE / #XM */
static void fx_save(CPU*c, u8*p);                      /* FXSAVE image: fxsave, signal frame (defined with the x87 code) */
static void fx_restore(CPU*c, const u8*p);
static const int gidx[16] = {8, 9, 10, 11, 12, 13, 14, 15, 7, 6, 5, 3, 2, 0, 1, 4};
#define SFRAME 1088
static void deliver_sig(CPU*c, int s) {
    struct gsa*h = &gsa[s];
    if (g_trace && s == SIGSEGV && g_fault == s && g_faddr < 0x10000) {                     /* a jump to a bogus address: who called, and with what */
        pemu_tr("pemu:   regs rax=%#llx rcx=%#llx rdx=%#llx rbx=%#llx rbp=%#llx rsi=%#llx rdi=%#llx r8=%#llx r9=%#llx r12=%#llx r13=%#llx r14=%#llx r15=%#llx\n",
                (unsigned long long) c->r[0], (unsigned long long) c->r[1], (unsigned long long) c->r[2], (unsigned long long) c->r[3], (unsigned long long) c->r[5],
                (unsigned long long) c->r[6], (unsigned long long) c->r[7], (unsigned long long) c->r[8], (unsigned long long) c->r[9], (unsigned long long) c->r[12],
                (unsigned long long) c->r[13], (unsigned long long) c->r[14], (unsigned long long) c->r[15]);
        for (int k = 0; k < 6; k++) {
            u64 w = ((u64*) c->r[RSP])[k]; char t[16]; snprintf(t, sizeof t, "[rsp+%d]", 8 * k);
            pemu_tr("pemu:   %s = %#llx\n", t, (unsigned long long) w); trace_map(t, w);
        }
        if (g_ringi) for (unsigned k = g_ringi > 24 ? g_ringi - 24 : 0; k < g_ringi; k++) {            /* the last instructions, oldest first (interpreter only: PEMU_JIT=0) */
            const u8 *q = (const u8*) g_ring[k & 63];
            pemu_tr("pemu:   exec %#llx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", (unsigned long long) g_ring[k & 63], q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9]);
            if (k == g_ringi - 1 || k == g_ringi - 24) trace_map("insn", g_ring[k & 63]);
        }
    }
    if (g_trace && (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE || s == SIGABRT || s == SIGTRAP))          /* diagnostics: what the guest's own handler is about to see */
        pemu_tr("pemu: signal %d to guest handler: rip=%#llx fault addr=%#llx code=%d (rsp=%#llx)\n", s, (unsigned long long) c->rip,
                (unsigned long long)(g_fault == s ? g_faddr : 0), pinfo[s].si_code, (unsigned long long) c->r[RSP]);
    int on_alt = g_ss_size && c->r[RSP] >= g_ss_sp && c->r[RSP] - g_ss_sp < g_ss_size;     /* interrupted code already runs on the alternate stack */
    u64 top = ((h->flags & 0x08000000u /*SA_ONSTACK*/) && g_ss_size && !on_alt) ? g_ss_sp + g_ss_size : c->r[RSP] - 128;
    u64 sp = ((top - SFRAME) & ~15ULL) - 8;
    g_in_frame = 1;                                                              /* a fault from here to the end of the frame kills the process, see hsegv */
    memset((void*) sp, 0, SFRAME);
    u64 uc = sp + 8, info = sp + 8 + 424, fp = sp + 568;
    memcpy((void*) info, &pinfo[s], 128);
    u64 old = 0;
    host_syscall_e(HOP_rt_sigprocmask, SIG_BLOCK, NULL, &old, 8);
    *(u64*)(uc + 296) = old;
    *(u64*)(uc + 16) = g_ss_sp; *(u32*)(uc + 24) = !g_ss_size ? 2 /*SS_DISABLE*/ : on_alt ? 1 /*SS_ONSTACK*/ : 0; *(u64*)(uc + 32) = g_ss_size;   /* uc_stack */
    u64 mc = uc + 40;
    for (int i = 0; i < 16; i++) *(u64*)(mc + 8 * (u64) i) = c->r[gidx[i]];
    *(u64*)(mc + 8 * 16) = c->rip; *(u64*)(mc + 8 * 17) = RF(c);
    *(u64*)(mc + 8 * 22) = g_fault == s ? g_faddr : 0;
    {   /* gregs[REG_ERR]=19, gregs[REG_TRAPNO]=20: Wine's segv_handler dispatches on the trap number */
        u64 trap = 0, err = 0;
        switch (s) {
        case SIGSEGV: case SIGBUS:
            if (pinfo[s].si_code == 0x80) trap = 13;                                    /* #GP from a privileged instruction */
            else if (g_fault == s) { trap = 14; err = 4 | (g_fwrite ? 2 : 0) | (pinfo[s].si_code == 2 ? 1 : 0); }   /* #PF: user, write?, protection? */
            break;
        case SIGILL: trap = 6; break;                                                   /* #UD */
        case SIGTRAP: trap = 3; break;                                                  /* #BP */
        case SIGFPE: trap = g_mf ? 16 : (pinfo[s].si_code == 1 || pinfo[s].si_code == 2) ? 0 : 19; break;   /* #MF, #DE, else #XM */
        default: break;
        }
        *(u64*)(mc + 8 * 19) = err; *(u64*)(mc + 8 * 20) = trap;
    }
    *(u64*)(mc + 8 * 23) = fp;
    fx_save(c, (u8*) fp);
    g_mf = 0;
    c->fcw = 0x37f; c->fsw = 0; c->top = 0; c->ftw = 0; c->mxcsr = 0x1f80;                     /* the handler starts with a default FPU, as on Linux; sigreturn restores the interrupted state */
    u64 nm = (old | h->mask | ((h->flags & 0x40000000) ? 0 : (1ULL << (s - 1)))) & ~(1ULL << (SIGSYS - 1));   /* never SIGSYS: see hsys */
    host_syscall_e(HOP_rt_sigprocmask, SIG_SETMASK, &nm, NULL, 8);
    *(u64*) sp = (h->flags & 0x04000000) ? h->restorer : g_tramp;
    g_in_frame = 0;
    c->r[RSP] = sp; c->r[RDI] = (u64) s; c->r[RSI] = info; c->r[RDX] = uc; c->r[RAX] = 0;
    c->rip = h->handler; RF(c) &= ~(u64)DF_;
    if (h->flags & 0x80000000u) h->handler = 0;
    g_fault = 0;
}
static void g_sigreturn(CPU*c) {
    u64 sp = c->r[RSP] - 8, uc = sp + 8, mc = uc + 40, fp = *(u64*)(mc + 8 * 23);
    for (int i = 0; i < 16; i++) c->r[gidx[i]] = *(u64*)(mc + 8 * (u64) i);
    c->rip = *(u64*)(mc + 8 * 16); RF(c) = *(u64*)(mc + 8 * 17);
    if (fp) fx_restore(c, (const u8*) fp);
    u64 m = *(u64*)(uc + 296);
    host_syscall_e(HOP_rt_sigprocmask, SIG_SETMASK, &m, NULL, 8);
}

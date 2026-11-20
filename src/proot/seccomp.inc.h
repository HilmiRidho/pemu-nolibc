/* src/proot/seccomp.inc.h - seccomp for the GUEST: strict mode and classic-BPF filters, run by the emulator itself.
 * Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0
 *
 * A guest filter cannot be given to the host kernel: it holds the guest's syscall numbers and arch, the emulator runs another ABI, and
 * the first emulator syscall after it would be killed.  But every syscall of the guest passes through gsys() (the guest has no vDSO),
 * so the filter is simply evaluated there, before anything else, on the numbers and arguments the guest used.
 *
 *   prctl(PR_SET_NO_NEW_PRIVS / PR_GET_NO_NEW_PRIVS)   a flag of the guest thread (the host process is not touched)
 *   prctl(PR_SET_SECCOMP / PR_GET_SECCOMP), seccomp(SET_MODE_STRICT / SET_MODE_FILTER / GET_ACTION_AVAIL)
 *   the filter: validated like the kernel does (bpf_check_classic + seccomp_check_filter), then run by a ~60 line cBPF interpreter
 *
 * Actions: ALLOW, LOG (as ALLOW), ERRNO (data clamped to 4095), TRAP (SIGSYS to the guest handler, siginfo as on Linux; no handler:
 * the process dies), KILL_THREAD, KILL_PROCESS and any unknown action (SIGSYS), TRACE and USER_NOTIF (no tracer, no supervisor: the
 * call answers ENOSYS, as on Linux without one).  All filters run; the lowest action wins, the newest filter wins a tie (kernel rule).
 *
 * Inheritance, as on Linux: fork and clone copy the filters of the calling thread; execve keeps them (see pemu_sc_export: the new
 * pemu image is handed them through a sealed memfd, and if that fails the exec fails - never an unfiltered image).  A TSYNC filter is
 * kept in a process-wide list that every thread (also later ones) runs in addition to its own.
 *
 * Deliberately not done: NEW_LISTENER / USER_NOTIF (EINVAL; needs a supervisor), WAIT_KILLABLE_RECV (EINVAL), SECCOMP_GET_NOTIF_SIZES
 * (EINVAL), a filter on a guest thread that is created by something other than clone.  A guest filter is a policy for the guest's
 * syscalls, NOT a security boundary: pemu is not one (see README).  Calls that never reach gsys (none by design) are not filtered. */
#include <sys/uio.h>

#define PEMU_SC_RET_KILL_PROCESS 0x80000000u
#define PEMU_SC_RET_KILL_THREAD  0x00000000u
#define PEMU_SC_RET_TRAP         0x00030000u
#define PEMU_SC_RET_ERRNO        0x00050000u
#define PEMU_SC_RET_USER_NOTIF   0x7fc00000u
#define PEMU_SC_RET_TRACE        0x7ff00000u
#define PEMU_SC_RET_LOG          0x7ffc0000u
#define PEMU_SC_RET_ALLOW        0x7fff0000u
#define PEMU_SC_ACTION_FULL      0xffff0000u
#define PEMU_SC_DATA             0x0000ffffu
#define PEMU_SC_MAXINSNS         4096u                /* BPF_MAXINSNS: instructions of one filter */
#define PEMU_SC_PATH_INSNS       (1ul << 18)          /* MAX_INSNS_PER_PATH: all the filters of one thread, 4 extra per filter */

/* cBPF opcodes (own names: <linux/filter.h> is not needed to build) */
#define PB_LD   0x00
#define PB_LDX  0x01
#define PB_ST   0x02
#define PB_STX  0x03
#define PB_ALU  0x04
#define PB_JMP  0x05
#define PB_RET  0x06
#define PB_MISC 0x07
#define PB_W    0x00
#define PB_IMM  0x00
#define PB_ABS  0x20
#define PB_MEM  0x60
#define PB_LEN  0x80
#define PB_K    0x00
#define PB_X    0x08
#define PB_A    0x10
#define PB_ADD  0x00
#define PB_SUB  0x10
#define PB_MUL  0x20
#define PB_DIV  0x30
#define PB_OR   0x40
#define PB_AND  0x50
#define PB_LSH  0x60
#define PB_RSH  0x70
#define PB_NEG  0x80
#define PB_MOD  0x90
#define PB_XOR  0xa0
#define PB_JA   0x00
#define PB_JEQ  0x10
#define PB_JGT  0x20
#define PB_JGE  0x30
#define PB_JSET 0x40
#define PB_TAX  0x00
#define PB_TXA  0x80

struct pemu_bpf { uint16_t code; uint8_t jt, jf; uint32_t k; };                      /* struct sock_filter */
struct pemu_sc_data { int32_t nr; uint32_t arch; uint64_t ip; uint64_t args[6]; };   /* struct seccomp_data: 64 bytes */
struct pemu_sc_node { const struct pemu_sc_node *next; struct pemu_bpf *insn; uint32_t len; uint64_t seq; };   /* immutable once published, never freed */
struct pemu_sc_task { const struct pemu_sc_node *own; int nnp, strict; };            /* what a new thread inherits */

static __thread struct pemu_sc_task pemu_sct;
static const struct pemu_sc_node *pemu_sc_sync;     /* TSYNC filters, and every filter handed over by an exec: run by all threads */
static int pemu_sc_gnnp, pemu_sc_gstrict;           /* the same for no_new_privs / strict mode */
static uint64_t pemu_sc_seqno;
static int pemu_sc_fd = -1;                         /* hidden --seccomp-fd: filters of the image that exec'd this one */

#define PEMU_SC_ACTIVE() __builtin_expect(pemu_sct.own != NULL || __atomic_load_n(&pemu_sc_sync, __ATOMIC_ACQUIRE) != NULL || pemu_sct.strict || pemu_sc_gstrict, 0)
static int pemu_sc_nnp(void) { return pemu_sct.nnp || pemu_sc_gnnp; }

/* ---- validation: what the kernel's bpf_check_classic() and seccomp_check_filter() accept (MOD is accepted too; libseccomp never emits it) */
static int pemu_sc_check(const struct pemu_bpf *f, unsigned len) {
    if (len == 0 || len > PEMU_SC_MAXINSNS) return -EINVAL;
    for (unsigned pc = 0; pc < len; pc++) {
        uint32_t k = f[pc].k;
        switch (f[pc].code) {
        case PB_LD | PB_W | PB_ABS: if (k >= sizeof(struct pemu_sc_data) || (k & 3)) return -EINVAL; break;
        case PB_LD | PB_W | PB_LEN: case PB_LDX | PB_W | PB_LEN: case PB_LD | PB_IMM: case PB_LDX | PB_IMM:
        case PB_MISC | PB_TAX: case PB_MISC | PB_TXA: case PB_RET | PB_K: case PB_RET | PB_A: break;
        case PB_LD | PB_MEM: case PB_LDX | PB_MEM: case PB_ST: case PB_STX: if (k >= 16) return -EINVAL; break;
        case PB_ALU | PB_DIV | PB_K: case PB_ALU | PB_MOD | PB_K: if (k == 0) return -EINVAL; break;
        case PB_ALU | PB_LSH | PB_K: case PB_ALU | PB_RSH | PB_K: if (k >= 32) return -EINVAL; break;
        case PB_ALU | PB_ADD | PB_K: case PB_ALU | PB_ADD | PB_X: case PB_ALU | PB_SUB | PB_K: case PB_ALU | PB_SUB | PB_X:
        case PB_ALU | PB_MUL | PB_K: case PB_ALU | PB_MUL | PB_X: case PB_ALU | PB_DIV | PB_X: case PB_ALU | PB_MOD | PB_X:
        case PB_ALU | PB_AND | PB_K: case PB_ALU | PB_AND | PB_X: case PB_ALU | PB_OR | PB_K: case PB_ALU | PB_OR | PB_X:
        case PB_ALU | PB_XOR | PB_K: case PB_ALU | PB_XOR | PB_X: case PB_ALU | PB_LSH | PB_X: case PB_ALU | PB_RSH | PB_X:
        case PB_ALU | PB_NEG: break;
        case PB_JMP | PB_JA: if (k >= len - pc - 1) return -EINVAL; break;
        case PB_JMP | PB_JEQ | PB_K: case PB_JMP | PB_JEQ | PB_X: case PB_JMP | PB_JGT | PB_K: case PB_JMP | PB_JGT | PB_X:
        case PB_JMP | PB_JGE | PB_K: case PB_JMP | PB_JGE | PB_X: case PB_JMP | PB_JSET | PB_K: case PB_JMP | PB_JSET | PB_X:
            if (pc + f[pc].jt + 1u >= len || pc + f[pc].jf + 1u >= len) return -EINVAL;
            break;
        default: return -EINVAL;
        }
    }
    return (f[len - 1].code & 7) == PB_RET ? 0 : -EINVAL;
}

/* ---- the interpreter.  A runtime division by zero ends the filter with 0 (= KILL_THREAD), as in the kernel. */
static uint32_t pemu_sc_run(const struct pemu_bpf *f, unsigned len, const struct pemu_sc_data *d) {
    uint32_t A = 0, X = 0, M[16] = {0};
    const uint8_t *dp = (const uint8_t *) d;
    for (unsigned pc = 0; pc < len; pc++) {
        const struct pemu_bpf *i = &f[pc]; uint32_t k = i->k;
        switch (i->code) {
        case PB_LD | PB_W | PB_ABS: memcpy(&A, dp + k, 4); break;
        case PB_LD | PB_W | PB_LEN: A = sizeof *d; break;
        case PB_LDX | PB_W | PB_LEN: X = sizeof *d; break;
        case PB_LD | PB_IMM: A = k; break;
        case PB_LDX | PB_IMM: X = k; break;
        case PB_LD | PB_MEM: A = M[k & 15]; break;
        case PB_LDX | PB_MEM: X = M[k & 15]; break;
        case PB_ST: M[k & 15] = A; break;
        case PB_STX: M[k & 15] = X; break;
        case PB_MISC | PB_TAX: X = A; break;
        case PB_MISC | PB_TXA: A = X; break;
        case PB_ALU | PB_ADD | PB_K: A += k; break;     case PB_ALU | PB_ADD | PB_X: A += X; break;
        case PB_ALU | PB_SUB | PB_K: A -= k; break;     case PB_ALU | PB_SUB | PB_X: A -= X; break;
        case PB_ALU | PB_MUL | PB_K: A *= k; break;     case PB_ALU | PB_MUL | PB_X: A *= X; break;
        case PB_ALU | PB_DIV | PB_K: A /= k; break;     case PB_ALU | PB_DIV | PB_X: if (!X) return 0; A /= X; break;
        case PB_ALU | PB_MOD | PB_K: A %= k; break;     case PB_ALU | PB_MOD | PB_X: if (!X) return 0; A %= X; break;
        case PB_ALU | PB_AND | PB_K: A &= k; break;     case PB_ALU | PB_AND | PB_X: A &= X; break;
        case PB_ALU | PB_OR | PB_K: A |= k; break;      case PB_ALU | PB_OR | PB_X: A |= X; break;
        case PB_ALU | PB_XOR | PB_K: A ^= k; break;     case PB_ALU | PB_XOR | PB_X: A ^= X; break;
        case PB_ALU | PB_LSH | PB_K: A <<= k; break;    case PB_ALU | PB_LSH | PB_X: A <<= (X & 31); break;
        case PB_ALU | PB_RSH | PB_K: A >>= k; break;    case PB_ALU | PB_RSH | PB_X: A >>= (X & 31); break;
        case PB_ALU | PB_NEG: A = -A; break;
        case PB_JMP | PB_JA: pc += k; break;
        case PB_JMP | PB_JEQ | PB_K: pc += A == k ? i->jt : i->jf; break;   case PB_JMP | PB_JEQ | PB_X: pc += A == X ? i->jt : i->jf; break;
        case PB_JMP | PB_JGT | PB_K: pc += A > k ? i->jt : i->jf; break;    case PB_JMP | PB_JGT | PB_X: pc += A > X ? i->jt : i->jf; break;
        case PB_JMP | PB_JGE | PB_K: pc += A >= k ? i->jt : i->jf; break;   case PB_JMP | PB_JGE | PB_X: pc += A >= X ? i->jt : i->jf; break;
        case PB_JMP | PB_JSET | PB_K: pc += (A & k) ? i->jt : i->jf; break; case PB_JMP | PB_JSET | PB_X: pc += (A & X) ? i->jt : i->jf; break;
        case PB_RET | PB_K: return k;
        case PB_RET | PB_A: return A;
        default: return 0;
        }
    }
    return 0;
}

/* all the filters, newest wins ties; actions compare as signed 32 bit (KILL_PROCESS 0x80000000 is the lowest) */
static uint32_t pemu_sc_decide(const struct pemu_sc_data *d) {
    uint32_t best = PEMU_SC_RET_ALLOW; uint64_t bseq = 0;
    const struct pemu_sc_node *l[2] = { pemu_sct.own, __atomic_load_n(&pemu_sc_sync, __ATOMIC_ACQUIRE) };
    for (int s = 0; s < 2; s++)
        for (const struct pemu_sc_node *n = l[s]; n; n = n->next) {
            uint32_t r = pemu_sc_run(n->insn, n->len, d);
            int32_t ra = (int32_t) (r & PEMU_SC_ACTION_FULL), ba = (int32_t) (best & PEMU_SC_ACTION_FULL);
            if (ra < ba || (ra == ba && n->seq > bseq)) { best = r; bseq = n->seq; }
        }
    return best;
}

/* ---- the check done by gsys() for every guest syscall */
enum { PEMU_SC_RUN = 0, PEMU_SC_DONE, PEMU_SC_TRAP, PEMU_SC_KILLT, PEMU_SC_KILLP };
static uint32_t pemu_sc_audit(void) { return 0xC000003Eu; }   /* AUDIT_ARCH_X86_64 */
/* RUN: let the call go on.  DONE: answer *val.  TRAP: SIGSYS with si_errno *val.  KILLT / KILLP: end the thread / process with signal *val. */
static int pemu_sc_enter(uint64_t n, uint64_t ip, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, long *val) {
    if (pemu_sct.strict || pemu_sc_gstrict) {          /* mode 1: read, write, exit, sigreturn; anything else is SIGKILL */
        static const uint16_t ok[4] = { 0, 1, 60, 15 };   /* read, write, exit, rt_sigreturn */
        for (int i = 0; i < 4; i++) if (n == ok[i]) return PEMU_SC_RUN;
        *val = SIGKILL; return PEMU_SC_KILLP;
    }
    struct pemu_sc_data d = { (int32_t) n, pemu_sc_audit(), ip, { a0, a1, a2, a3, a4, a5 } };
    uint32_t ret = pemu_sc_decide(&d), data = ret & PEMU_SC_DATA;
    switch (ret & PEMU_SC_ACTION_FULL) {
    case PEMU_SC_RET_ALLOW: case PEMU_SC_RET_LOG: return PEMU_SC_RUN;
    case PEMU_SC_RET_ERRNO: *val = -(long) (data > 4095 ? 4095 : data); return PEMU_SC_DONE;
    case PEMU_SC_RET_TRAP: *val = (long) data; return PEMU_SC_TRAP;
    case PEMU_SC_RET_KILL_THREAD: *val = SIGSYS; return PEMU_SC_KILLT;
    case PEMU_SC_RET_TRACE: case PEMU_SC_RET_USER_NOTIF: *val = -ENOSYS; return PEMU_SC_DONE;   /* nobody to ask: the kernel answers ENOSYS */
    default: *val = SIGSYS; return PEMU_SC_KILLP;      /* KILL_PROCESS and every unknown action */
    }
}
/* the siginfo of a SECCOMP_RET_TRAP, written raw so that no libc version matters: si_call_addr, si_syscall, si_arch at 16 / 24 / 28 */
static void pemu_sc_siginfo(siginfo_t *si, uint32_t data, uint64_t n, uint64_t ip) {
    uint8_t *p = (uint8_t *) si;
    memset(si, 0, sizeof *si);
    *(int32_t *) p = SIGSYS; *(int32_t *) (p + 4) = (int32_t) data; *(int32_t *) (p + 8) = 1 /*SYS_SECCOMP*/;
    *(uint64_t *) (p + 16) = ip; *(int32_t *) (p + 24) = (int32_t) n; *(uint32_t *) (p + 28) = pemu_sc_audit();
}
/* the process ends by the signal, as a killed process does for its parent (the same way a fatal guest fault does) */
static void pemu_sc_die(int sig, uint64_t n, uint64_t ip) {
    plog_ev("guest-fault", n, "seccomp: syscall %llu at rip=%#llx is killed by the guest's filter (signal %d)", (unsigned long long) n, (unsigned long long) ip, sig);
    plog_summary();
    signal(sig, SIG_DFL);
    sigset_t ss; sigemptyset(&ss); sigaddset(&ss, sig); sigprocmask(SIG_UNBLOCK, &ss, NULL);
    host_raise(sig); host_exit(128 + sig);
}

/* ---- installing */
static int pemu_sc_copyin(void *dst, const void *src, size_t n) {          /* EFAULT instead of a crash, where the host allows process_vm_readv on ourselves */
    if (!src) return -EFAULT;
    struct iovec l = { dst, n }, r = { (void *) src, n };
    long k = host_syscall_e(HOP_process_vm_readv, (long) host_getpid(), &l, 1L, &r, 1L, 0L);
    if (k == (long) n) return 0;
    if (k >= 0 || errno == EFAULT) return -EFAULT;
    memcpy(dst, src, n);                                                     /* no process_vm_readv here (old kernel, host seccomp): trust the pointer, as the other hooks do */
    return 0;
}
static unsigned long pemu_sc_insns(const struct pemu_sc_node *n) { unsigned long t = 0; for (; n; n = n->next) t += n->len + 4ul; return t; }
static void pemu_sc_push_sync(struct pemu_sc_node *nd) {
    const struct pemu_sc_node *h = __atomic_load_n(&pemu_sc_sync, __ATOMIC_RELAXED);
    do nd->next = h; while (!__atomic_compare_exchange_n(&pemu_sc_sync, &h, nd, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}
static long pemu_sc_add(const struct pemu_bpf *src, unsigned len, int sync, uint64_t seq) {
    struct pemu_bpf *insn = malloc((size_t) len * sizeof *insn);
    struct pemu_sc_node *nd = malloc(sizeof *nd);
    if (!insn || !nd) { free(insn); free(nd); return -ENOMEM; }
    memcpy(insn, src, (size_t) len * sizeof *insn);
    nd->insn = insn; nd->len = len; nd->seq = seq ? seq : __atomic_add_fetch(&pemu_sc_seqno, 1, __ATOMIC_RELAXED);
    if (sync) pemu_sc_push_sync(nd);
    else { nd->next = pemu_sct.own; pemu_sct.own = nd; }
    return 0;
}
/* seccomp(SECCOMP_SET_MODE_FILTER, flags, struct sock_fprog *) */
static long pemu_sc_set_filter(unsigned flags, const void *uprog) {
    if (flags & ~(1u | 2u | 4u | 16u)) {                                     /* TSYNC, LOG, SPEC_ALLOW, TSYNC_ESRCH; NEW_LISTENER (8) and WAIT_KILLABLE_RECV (32) are not here */
        if (flags & (8u | 32u)) return PLOG_REFUSE(EINVAL, "seccomp: NEW_LISTENER / WAIT_KILLABLE_RECV (user notification) is not supported (EINVAL)");
        return -EINVAL;
    }
    if ((flags & 16u) && !(flags & 1u)) return -EINVAL;
    if (!pemu_sc_nnp() && !(pemu_fakeroot && pemu_uid[PEMU_E] == 0)) return -EACCES;   /* no CAP_SYS_ADMIN, no no_new_privs: as the kernel */
    if (pemu_sct.strict || pemu_sc_gstrict) return -EINVAL;
    uint8_t fp[16]; int e = pemu_sc_copyin(fp, uprog, sizeof fp);            /* struct sock_fprog { u16 len; struct sock_filter *filter; } */
    if (e) return e;
    unsigned len = *(uint16_t *) fp; const void *up = *(const void **) (fp + 8);
    if (len == 0 || len > PEMU_SC_MAXINSNS) return -EINVAL;
    struct pemu_bpf *f = malloc((size_t) len * sizeof *f);
    if (!f) return -ENOMEM;
    if ((e = pemu_sc_copyin(f, up, (size_t) len * sizeof *f)) != 0 || (e = pemu_sc_check(f, len)) != 0) { free(f); return e; }
    if (pemu_sc_insns(pemu_sct.own) + pemu_sc_insns(__atomic_load_n(&pemu_sc_sync, __ATOMIC_ACQUIRE)) + len + 4ul > PEMU_SC_PATH_INSNS) { free(f); return -ENOMEM; }
    long r = pemu_sc_add(f, len, (flags & 1u) != 0, 0);
    free(f);
    if (r == 0 && (flags & 1u)) __atomic_store_n(&pemu_sc_gnnp, 1, __ATOMIC_RELAXED);   /* TSYNC: every thread gets no_new_privs with it */
    return r;
}
static long pemu_sc_set_strict(void) {
    if (pemu_sct.own || __atomic_load_n(&pemu_sc_sync, __ATOMIC_ACQUIRE)) return -EINVAL;   /* already in filter mode */
    pemu_sct.strict = 1;
    return 0;
}
static int pemu_sc_mode(void) { return (pemu_sct.strict || pemu_sc_gstrict) ? 1 : (pemu_sct.own || __atomic_load_n(&pemu_sc_sync, __ATOMIC_ACQUIRE)) ? 2 : 0; }
static long pemu_sc_seccomp(unsigned op, unsigned flags, const void *uargs) {
    switch (op) {
    case 0: return (flags || uargs) ? -EINVAL : pemu_sc_set_strict();       /* SECCOMP_SET_MODE_STRICT */
    case 1: return pemu_sc_set_filter(flags, uargs);                          /* SECCOMP_SET_MODE_FILTER */
    case 2: {                                                                 /* SECCOMP_GET_ACTION_AVAIL */
        uint32_t a; int e;
        if (flags) return -EINVAL;
        if ((e = pemu_sc_copyin(&a, uargs, 4)) != 0) return e;
        switch (a) { case PEMU_SC_RET_KILL_PROCESS: case PEMU_SC_RET_KILL_THREAD: case PEMU_SC_RET_TRAP: case PEMU_SC_RET_ERRNO:
                     case PEMU_SC_RET_TRACE: case PEMU_SC_RET_LOG: case PEMU_SC_RET_ALLOW: return 0; default: return -EOPNOTSUPP; }
    }
    default: return -EINVAL;                                                  /* 3 = GET_NOTIF_SIZES: no user notification here */
    }
}
/* the guest's seccomp / prctl calls (x86_64 numbers: seccomp 317, prctl 157); 1 = answered in *r. */
static int pemu_sc_call(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, long *r) {
    enum { NR_SECCOMP = 317, NR_PRCTL = 157 };
    if (n == NR_SECCOMP) { *r = pemu_sc_seccomp((unsigned) a0, (unsigned) a1, (const void *) a2); return 1; }
    if (n != NR_PRCTL) return 0;
    switch ((int) a0) {
    case 21: *r = pemu_sc_mode(); return 1;                                       /* PR_GET_SECCOMP */
    case 22: *r = a1 == 1 ? pemu_sc_set_strict() : a1 == 2 ? pemu_sc_set_filter(0, (const void *) a2) : -EINVAL; return 1;   /* PR_SET_SECCOMP */
    case 38: if (a1 != 1 || a2 || a3 || a4) *r = -EINVAL; else { pemu_sct.nnp = 1; *r = 0; } return 1;     /* PR_SET_NO_NEW_PRIVS */
    case 39: *r = (a1 || a2 || a3 || a4) ? -EINVAL : pemu_sc_nnp(); return 1;     /* PR_GET_NO_NEW_PRIVS */
    }
    return 0;
}

/* ---- execve: the new pemu image is a new process and must run the same filters before its first guest instruction.
 * Handed over as a sealed memfd (nobody can edit it between here and the exec), named by the hidden option --seccomp-fd. */
#define PEMU_SC_MAGIC 0x31435350u                          /* "PSC1" */
struct pemu_sc_hdr { uint32_t magic, nnp, strict, count; };
struct pemu_sc_rec { uint64_t seq; uint32_t len, pad; };
static long pemu_sc_export(int *fdout) {                   /* 0, with *fdout >= 0 when there is something to hand over; or -errno: the exec must fail */
    const struct pemu_sc_node *l[2] = { pemu_sct.own, __atomic_load_n(&pemu_sc_sync, __ATOMIC_ACQUIRE) };
    size_t total = sizeof(struct pemu_sc_hdr); uint32_t cnt = 0;
    *fdout = -1;
    for (int s = 0; s < 2; s++) for (const struct pemu_sc_node *n = l[s]; n; n = n->next) { total += sizeof(struct pemu_sc_rec) + (size_t) n->len * sizeof(struct pemu_bpf); cnt++; }
    int strict = pemu_sct.strict || pemu_sc_gstrict, nnp = pemu_sc_nnp();
    if (!cnt && !strict && !nnp) return 0;
    uint8_t *b = malloc(total), *p = b;
    if (!b) return -ENOMEM;
    struct pemu_sc_hdr h = { PEMU_SC_MAGIC, (uint32_t) nnp, (uint32_t) strict, cnt };
    memcpy(p, &h, sizeof h); p += sizeof h;
    for (int s = 0; s < 2; s++) for (const struct pemu_sc_node *n = l[s]; n; n = n->next) {
        struct pemu_sc_rec r = { n->seq, n->len, 0 };
        memcpy(p, &r, sizeof r); p += sizeof r;
        memcpy(p, n->insn, (size_t) n->len * sizeof(struct pemu_bpf)); p += (size_t) n->len * sizeof(struct pemu_bpf);
    }
    long fd = host_syscall_e(HOP_memfd_create, "pemu-seccomp", 2 /*MFD_ALLOW_SEALING; not close-on-exec: the new image reads it*/);
    long res = 0;
    if (fd >= 0) {
        if (host_write((int) fd, b, total) != (ssize_t) total || host_lseek((int) fd, 0, SEEK_SET) < 0) res = -EIO;
        else if (host_fcntl((int) fd, 1033 /*F_ADD_SEALS*/, 1 | 2 | 4 | 8 /*SEAL | SHRINK | GROW | WRITE*/) < 0) res = -errno;   /* unsealed would let a guest thread rewrite it */
    } else {                                               /* no memfd (blocked, old kernel): a pipe whose write end is closed cannot be written by anybody */
        int pp[2];
        if (host_pipe2(pp, O_CLOEXEC) < 0) res = -errno;
        else {
            host_fcntl(pp[1], F_SETPIPE_SZ, (int) total + 4096);
            host_fcntl(pp[1], F_SETFL, O_NONBLOCK);
            if (host_write(pp[1], b, total) != (ssize_t) total) res = -E2BIG;
            host_close(pp[1]);
            if (res == 0) { fd = host_fcntl(pp[0], F_DUPFD, 3); if (fd < 0) res = -errno; }   /* F_DUPFD: not close-on-exec */
            host_close(pp[0]);
        }
    }
    free(b);
    if (res < 0) { if (fd >= 0) host_close((int) fd); return res; }
    *fdout = (int) fd;
    return 0;
}
static int pemu_sc_import(void) {                          /* the first thing after the options: 0, or -1 (the image must not run) */
    int fd = pemu_sc_fd; pemu_sc_fd = -1;
    if (fd < 0) return 0;
    size_t cap = 1 << 16, len = 0; uint8_t *b = malloc(cap);
    int ok = 0;
    while (b) {
        if (len == cap) { if (cap >= (16u << 20)) break; cap *= 2; uint8_t *nb = realloc(b, cap); if (!nb) break; b = nb; }
        ssize_t r = host_read(fd, b + len, cap - len);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) break;
        if (r == 0) { ok = 1; break; }
        len += (size_t) r;
    }
    host_close(fd);
    struct pemu_sc_hdr h; const uint8_t *p = b;
    if (!ok || len < sizeof h) goto bad;
    memcpy(&h, p, sizeof h); p += sizeof h;
    if (h.magic != PEMU_SC_MAGIC || h.count > 4096) goto bad;
    for (uint32_t i = 0; i < h.count; i++) {
        struct pemu_sc_rec r;
        if ((size_t) (b + len - p) < sizeof r) goto bad;
        memcpy(&r, p, sizeof r); p += sizeof r;
        if (r.len == 0 || r.len > PEMU_SC_MAXINSNS || (size_t) (b + len - p) < (size_t) r.len * sizeof(struct pemu_bpf)) goto bad;
        struct pemu_bpf *f = malloc((size_t) r.len * sizeof *f);
        if (!f) goto bad;
        memcpy(f, p, (size_t) r.len * sizeof *f); p += (size_t) r.len * sizeof *f;
        long e = pemu_sc_check(f, r.len);
        if (e == 0) e = pemu_sc_add(f, r.len, 1, r.seq ? r.seq : 1);
        free(f);
        if (e) goto bad;
        if (r.seq > pemu_sc_seqno) pemu_sc_seqno = r.seq;
    }
    pemu_sc_gnnp = h.nnp != 0; pemu_sc_gstrict = h.strict != 0;
    free(b); return 0;
bad:
    free(b);
    fprintf(stderr, "pemu: the seccomp filters of the previous image could not be restored; not running without them\n");
    return -1;
}

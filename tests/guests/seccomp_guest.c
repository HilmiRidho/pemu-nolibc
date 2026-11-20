/* tests/guests/seccomp_guest.c - a freestanding x86_64 guest that uses seccomp for real: no_new_privs, a filter with ERRNO / TRAP / ALLOW,
 * a SIGSYS handler that sets the result, fork and execve inheritance, KILL_PROCESS, strict mode.  Built with gcc, run by tests/integration/seccomp.sh. */
typedef unsigned long u64; typedef long s64; typedef unsigned u32; typedef unsigned short u16; typedef unsigned char u8;
#define SYS(n, a, b, c, d) sc4((n), (long) (a), (long) (b), (long) (c), (long) (d))
static long sc4(long n, long a, long b, long c, long d) {
    long r; register long r10 __asm__("r10") = d, r8 __asm__("r8") = 0, r9 __asm__("r9") = 0;   /* unused arguments are 0: prctl and seccomp check them */
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
static void put(const char *s) { long n = 0; while (s[n]) n++; SYS(1, 1, s, n, 0); }
static void putnum(long v) { char b[24]; int i = 23; b[i] = 0; int neg = v < 0; u64 u = neg ? -(u64) v : (u64) v; do { b[--i] = '0' + u % 10; u /= 10; } while (u); if (neg) b[--i] = '-'; put(b + i); }
static int bad;
static void check(const char *name, int ok, long got) { put(name); if (ok) put(": ok\n"); else { bad = 1; put(": FAIL got="); putnum(got); put("\n"); } }
static void die(int code) { SYS(231, code, 0, 0, 0); for (;;) {} }
__asm__(".global restorer\nrestorer: mov $15,%eax\n syscall\n");
extern void restorer(void);

struct bpf { u16 code; u8 jt, jf; u32 k; };
struct fprog { u16 len; u16 p1; u32 p2; const struct bpf *f; };
#define LD(off)       { 0x20, 0, 0, (off) }
#define JEQ(k, jt, jf) { 0x15, (jt), (jf), (k) }
#define RET(k)        { 0x06, 0, 0, (k) }
#define ALLOW 0x7fff0000u
#define ERRNO(e) (0x00050000u | (e))
#define TRAP(d) (0x00030000u | (d))
#define KILLP 0x80000000u
static const struct bpf filter1[] = {
    LD(4), JEQ(0xC000003Eu, 1, 0), RET(KILLP),            /* wrong arch: kill */
    LD(0), JEQ(110, 3, 0), JEQ(102, 3, 0), JEQ(186, 3, 0), /* getppid, getuid, gettid */
    RET(ALLOW),
    RET(ERRNO(13)),                                        /* getppid -> EACCES */
    RET(TRAP(0x42)),                                       /* getuid  -> SIGSYS */
    RET(ERRNO(0)),                                         /* gettid  -> 0 without running */
};
static const struct bpf filter_kill[] = { LD(0), JEQ(39, 0, 1), RET(KILLP), RET(ALLOW) };   /* getpid: KILL_PROCESS */
static long install(const struct bpf *f, int n) { struct fprog p = { (u16) n, 0, 0, f }; return SYS(317, 1, 0, &p, 0); }

static volatile long h_code, h_errno, h_call, h_arch, h_addr, h_hits;
static void on_sys(int sig, u8 *si, u8 *uc) {
    (void) sig;
    h_errno = *(int *) (si + 4); h_code = *(int *) (si + 8); h_addr = *(long *) (si + 16); h_call = *(int *) (si + 24); h_arch = *(u32 *) (si + 28); h_hits++;
    *(u64 *) (uc + 40 + 13 * 8) = 777;                     /* uc_mcontext.gregs[REG_RAX]: the result of the trapped call */
}
struct ksa { void *h; u64 flags; void *rest; u64 mask; };

static int waitst(long pid) { int st = -1; SYS(61, pid, &st, 0, 0); return st; }
static int exited(int st, int code) { return (st & 0x7f) == 0 && ((st >> 8) & 0xff) == code; }
static int signaled(int st, int sig) { return (st & 0x7f) == sig; }

void cmain(long *sp) {
    long argc = sp[0]; char **argv = (char **) (sp + 1);
    long r;
    if (argc > 1 && argv[1][0] == 'a') {                   /* the image after execve: the filters must have come along */
        check("exec: PR_GET_SECCOMP", SYS(157, 21, 0, 0, 0) == 2, SYS(157, 21, 0, 0, 0));
        check("exec: PR_GET_NO_NEW_PRIVS", SYS(157, 39, 0, 0, 0) == 1, SYS(157, 39, 0, 0, 0));
        r = SYS(110, 0, 0, 0, 0); check("exec: getppid is EACCES", r == -13, r);
        r = SYS(186, 0, 0, 0, 0); check("exec: gettid is 0", r == 0, r);
        r = SYS(39, 0, 0, 0, 0); check("exec: getpid runs", r > 0, r);
        put("exec: getuid traps with no handler\n");
        SYS(102, 0, 0, 0, 0);                              /* no SIGSYS handler in a new image: the process dies */
        put("exec: FAIL survived\n"); die(1);
    }
    /* 1. strict mode first, in a child (a process cannot go from filter mode to strict mode) */
    long pid = SYS(57, 0, 0, 0, 0);
    if (pid == 0) {
        r = SYS(157, 22, 1, 0, 0);
        if (r != 0) { put("strict: FAIL prctl\n"); die(1); }
        put("strict: write works\n");
        SYS(39, 0, 0, 0, 0);                               /* getpid: SIGKILL */
        put("strict: FAIL survived\n"); die(1);
    }
    { int st = waitst(pid); check("strict: killed by SIGKILL", signaled(st, 9), st); }

    /* 2. no_new_privs, then the filter */
    r = install(filter1, 11); check("install without no_new_privs is EACCES", r == -13, r);
    r = SYS(157, 38, 0, 0, 0); check("PR_SET_NO_NEW_PRIVS arg 0 is EINVAL", r == -22, r);
    r = SYS(157, 38, 1, 0, 0); check("PR_SET_NO_NEW_PRIVS", r == 0, r);
    r = SYS(157, 21, 0, 0, 0); check("PR_GET_SECCOMP before: 0", r == 0, r);
    r = install(filter1, 11); check("install", r == 0, r);
    r = SYS(157, 21, 0, 0, 0); check("PR_GET_SECCOMP after: 2", r == 2, r);
    { u32 act = 0x00050000u; r = SYS(317, 2, 0, &act, 0); check("GET_ACTION_AVAIL ERRNO", r == 0, r);
      act = 0x7fc00000u; r = SYS(317, 2, 0, &act, 0); check("GET_ACTION_AVAIL USER_NOTIF is EOPNOTSUPP", r == -95, r); }
    r = SYS(317, 1, 8, 0, 0); check("NEW_LISTENER is EINVAL", r == -22, r);
    r = SYS(110, 0, 0, 0, 0); check("getppid is EACCES", r == -13, r);
    r = SYS(186, 0, 0, 0, 0); check("gettid is 0, not run", r == 0, r);
    r = SYS(39, 0, 0, 0, 0); check("getpid runs", r > 0, r);

    /* 3. TRAP: a SIGSYS handler sees the call and sets the result */
    { struct ksa sa = { (void *) on_sys, 0x4 /*SA_SIGINFO*/ | 0x04000000 /*SA_RESTORER*/, (void *) restorer, 0 };
      r = SYS(13, 31, &sa, 0, 8); check("sigaction(SIGSYS)", r == 0, r); }
    r = SYS(102, 0, 0, 0, 0);
    check("TRAP: the handler's result is the call's result", r == 777, r);
    check("TRAP: handler ran once", h_hits == 1, h_hits);
    check("TRAP: si_code is SYS_SECCOMP", h_code == 1, h_code);
    check("TRAP: si_errno is the filter data", h_errno == 0x42, h_errno);
    check("TRAP: si_syscall is getuid", h_call == 102, h_call);
    check("TRAP: si_arch is x86_64", h_arch == 0xC000003Eu, h_arch);
    check("TRAP: si_call_addr is set", h_addr != 0, h_addr);

    /* 4. fork: the child has the filters */
    pid = SYS(57, 0, 0, 0, 0);
    if (pid == 0) die((SYS(110, 0, 0, 0, 0) == -13 && SYS(157, 21, 0, 0, 0) == 2) ? 0 : 1);
    { int st = waitst(pid); check("fork: the child runs the filter", exited(st, 0), st); }

    /* 5. execve: the filters survive it (handed over to the new image) */
    pid = SYS(57, 0, 0, 0, 0);
    if (pid == 0) { char *av[3] = { argv[0], "after", 0 }; SYS(59, argv[0], av, 0, 0); put("execve: FAIL\n"); die(1); }
    { int st = waitst(pid); check("exec: the new image died of SIGSYS at getuid", signaled(st, 31), st); }

    /* 6. a second filter: KILL_PROCESS beats the first one's ALLOW */
    pid = SYS(57, 0, 0, 0, 0);
    if (pid == 0) {
        r = install(filter_kill, 4);
        if (r != 0) die(2);
        SYS(39, 0, 0, 0, 0);
        die(1);
    }
    { int st = waitst(pid); check("KILL_PROCESS: the process dies of SIGSYS", signaled(st, 31), st); }

    put(bad ? "RESULT: FAIL\n" : "RESULT: ok\n");
    die(bad);
}
__asm__(".global _start\n_start:\n mov %rsp,%rdi\n and $-16,%rsp\n call cmain\n");

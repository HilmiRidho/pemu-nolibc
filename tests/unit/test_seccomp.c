/* run: quiet-stderr */
/* tests/unit/test_seccomp.c - the guest's seccomp (src/proot/seccomp.inc.h): validation, the cBPF interpreter, action precedence,
 * installing (no_new_privs, strict, TSYNC, flags), inheritance by threads, and the hand-over across an exec (export -> import).
 * Host-native, no guest binaries; hs() never reaches the kernel. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/syscall.h>
static long hs(long nr, long a, long b, long c_, long d, long e, long f) { (void) nr; (void) a; (void) b; (void) c_; (void) d; (void) e; (void) f; return -ENOSYS; }
#include "pemu_root.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

#define I(c, jt, jf, k) ((struct pemu_bpf) { (c), (jt), (jf), (k) })
#define LD_NR   I(PB_LD | PB_W | PB_ABS, 0, 0, 0)
#define LD_ARCH I(PB_LD | PB_W | PB_ABS, 0, 0, 4)
#define LD_A0   I(PB_LD | PB_W | PB_ABS, 0, 0, 16)
#define LD_A0H  I(PB_LD | PB_W | PB_ABS, 0, 0, 20)
#define JEQ(k, jt, jf) I(PB_JMP | PB_JEQ | PB_K, jt, jf, k)
#define RET(k)  I(PB_RET | PB_K, 0, 0, k)
#define ARRSZ(a) (sizeof (a) / sizeof *(a))

static void reset(void) {                                  /* a thread with no filters, no flags */
    memset(&pemu_sct, 0, sizeof pemu_sct); pemu_sc_sync = NULL; pemu_sc_gnnp = pemu_sc_gstrict = 0; pemu_sc_seqno = 0;
}
static long install(const struct pemu_bpf *f, unsigned len, unsigned flags) {
    struct { uint16_t len; uint8_t pad[6]; const struct pemu_bpf *p; } fp = { (uint16_t) len, {0}, f };
    return pemu_sc_seccomp(1, flags, &fp);
}
/* what the filter says about one call: the verdict code, and *val */
static int enter(uint64_t n, uint64_t a0, long *val) { *val = 0; return pemu_sc_enter(n, 0x1000, a0, 0, 0, 0, 0, 0, val); }

static void *thr_check(void *arg) {                        /* a thread created after the filter runs it too (inherited copy, as clone_thread does) */
    struct pemu_sc_task *t = arg; long v;
    pemu_sct = *t;
    return (void *) (intptr_t) enter(39, 0, &v);
}

int main(void) {
    long v, r;
    /* 1. validation: what the kernel rejects */
    { struct pemu_bpf ok[] = { LD_NR, RET(PEMU_SC_RET_ALLOW) };
      CHECK(pemu_sc_check(ok, 2) == 0, "valid filter refused");
      CHECK(pemu_sc_check(ok, 0) == -EINVAL, "empty filter accepted");
      CHECK(pemu_sc_check(ok, 1) == -EINVAL, "a filter that does not end in RET accepted"); }
    { struct pemu_bpf big[PEMU_SC_MAXINSNS + 1]; for (unsigned i = 0; i < ARRSZ(big); i++) big[i] = RET(PEMU_SC_RET_ALLOW);
      CHECK(pemu_sc_check(big, PEMU_SC_MAXINSNS) == 0, "4096 instructions refused"); CHECK(pemu_sc_check(big, PEMU_SC_MAXINSNS + 1) == -EINVAL, "4097 accepted"); }
    { struct pemu_bpf a[] = { I(PB_LD | PB_W | PB_ABS, 0, 0, 2), RET(PEMU_SC_RET_ALLOW) };    CHECK(pemu_sc_check(a, 2) == -EINVAL, "misaligned load accepted");
      struct pemu_bpf b[] = { I(PB_LD | PB_W | PB_ABS, 0, 0, 64), RET(PEMU_SC_RET_ALLOW) };   CHECK(pemu_sc_check(b, 2) == -EINVAL, "load past seccomp_data accepted");
      struct pemu_bpf c[] = { I(PB_LD | 0x08 /*half*/ | PB_ABS, 0, 0, 0), RET(PEMU_SC_RET_ALLOW) }; CHECK(pemu_sc_check(c, 2) == -EINVAL, "16-bit load accepted");
      struct pemu_bpf d[] = { I(PB_ALU | PB_DIV | PB_K, 0, 0, 0), RET(PEMU_SC_RET_ALLOW) };   CHECK(pemu_sc_check(d, 2) == -EINVAL, "division by constant 0 accepted");
      struct pemu_bpf e[] = { I(PB_ST, 0, 0, 16), RET(PEMU_SC_RET_ALLOW) };                  CHECK(pemu_sc_check(e, 2) == -EINVAL, "scratch slot 16 accepted");
      struct pemu_bpf f[] = { JEQ(1, 5, 0), RET(PEMU_SC_RET_ALLOW) };                        CHECK(pemu_sc_check(f, 2) == -EINVAL, "jump past the end accepted");
      struct pemu_bpf g[] = { I(PB_JMP | PB_JA, 0, 0, 1), RET(PEMU_SC_RET_ALLOW) };          CHECK(pemu_sc_check(g, 2) == -EINVAL, "JA past the end accepted");
      struct pemu_bpf h[] = { I(0xff, 0, 0, 0), RET(PEMU_SC_RET_ALLOW) };                    CHECK(pemu_sc_check(h, 2) == -EINVAL, "unknown opcode accepted");
      struct pemu_bpf i[] = { I(PB_LD | PB_W | 0x40 /*IND*/, 0, 0, 0), RET(PEMU_SC_RET_ALLOW) }; CHECK(pemu_sc_check(i, 2) == -EINVAL, "indirect load accepted"); }

    /* 2. installing needs no_new_privs (or fake root) */
    struct pemu_bpf deny_getppid[] = { LD_ARCH, JEQ(0xC000003Eu, 1, 0), RET(PEMU_SC_RET_KILL_PROCESS), LD_NR, JEQ(110, 0, 1), RET(PEMU_SC_RET_ERRNO | 13), RET(PEMU_SC_RET_ALLOW) };
    reset(); pemu_fakeroot = 0;
    CHECK(install(deny_getppid, ARRSZ(deny_getppid), 0) == -EACCES, "install without no_new_privs: %ld", install(deny_getppid, ARRSZ(deny_getppid), 0));
    CHECK(pemu_sc_call(157, 39, 0, 0, 0, 0, &r) && r == 0, "PR_GET_NO_NEW_PRIVS starts at 0");
    CHECK(pemu_sc_call(157, 38, 0, 0, 0, 0, &r) && r == -EINVAL, "PR_SET_NO_NEW_PRIVS with arg 0 must be EINVAL");
    CHECK(pemu_sc_call(157, 38, 1, 0, 0, 0, &r) && r == 0, "PR_SET_NO_NEW_PRIVS");
    CHECK(pemu_sc_call(157, 39, 0, 0, 0, 0, &r) && r == 1, "PR_GET_NO_NEW_PRIVS after set");
    CHECK(pemu_sc_call(157, 15, 0, 0, 0, 0, &r) == 0, "an unrelated prctl must go on to the host");
    CHECK(pemu_sc_call(1, 0, 0, 0, 0, 0, &r) == 0, "an unrelated syscall must go on");
    CHECK(pemu_sc_call(167, 38, 1, 0, 0, 0, &r) == 0 && pemu_sc_call(277, 2, 0, 0, 0, 0, &r) == 0, "another ABI's prctl / seccomp numbers (AArch64 167 / 277) must not be answered");
    CHECK(!PEMU_SC_ACTIVE(), "active before any filter");
    CHECK(install(deny_getppid, ARRSZ(deny_getppid), 0) == 0, "install with no_new_privs");
    CHECK(PEMU_SC_ACTIVE(), "not active after install");
    CHECK(pemu_sc_call(157, 21, 0, 0, 0, 0, &r) && r == 2, "PR_GET_SECCOMP must say 2 (filter)");

    /* 3. the verdicts */
    CHECK(enter(110, 0, &v) == PEMU_SC_DONE && v == -13, "getppid -> EACCES: %d/%ld", enter(110, 0, &v), v);
    CHECK(enter(39, 0, &v) == PEMU_SC_RUN, "getpid must run");
    { struct pemu_sc_data d = { 110, 0xC00000B7u /*AUDIT_ARCH_AARCH64*/, 0x1000, {0} };
      CHECK((pemu_sc_decide(&d) & PEMU_SC_ACTION_FULL) == PEMU_SC_RET_KILL_PROCESS, "a wrong arch must be killed (an AArch64 call against an x86_64-only filter)"); }
    { struct pemu_sc_task snap = pemu_sct; pthread_t t; void *res;
      pthread_create(&t, NULL, thr_check, &snap); pthread_join(t, &res); CHECK((intptr_t) res == PEMU_SC_RUN, "thread: getpid");
      CHECK(pemu_sct.own == snap.own, "own list unchanged"); }
    CHECK(pemu_sc_call(317, 2, 0, 0, 0, 0, &r) && r == -EFAULT, "GET_ACTION_AVAIL with a NULL pointer: %ld", r);
    /* the filter looks at the arguments: low and high word of arg 0 */
    reset(); pemu_sc_gnnp = 1;
    { struct pemu_bpf f[] = { LD_A0H, JEQ(0x1, 0, 3), LD_A0, JEQ(0x2, 0, 1), RET(PEMU_SC_RET_ERRNO | 7), RET(PEMU_SC_RET_ALLOW) };
      CHECK(install(f, ARRSZ(f), 0) == 0, "args filter");
      CHECK(enter(1, 0x100000002ULL, &v) == PEMU_SC_DONE && v == -7, "a0 == 0x1_00000002 must be denied");
      CHECK(enter(1, 0x2, &v) == PEMU_SC_RUN, "a0 == 2 (high word 0) must run");
      CHECK(enter(1, 0x100000003ULL, &v) == PEMU_SC_RUN, "a0 == 0x1_00000003 must run"); }

    /* 4. every action */
    static const struct { uint32_t ret; int kind; long val; } acts[] = {
        { PEMU_SC_RET_ALLOW, PEMU_SC_RUN, 0 }, { PEMU_SC_RET_LOG, PEMU_SC_RUN, 0 }, { PEMU_SC_RET_ERRNO | 1, PEMU_SC_DONE, -1 }, { PEMU_SC_RET_ERRNO, PEMU_SC_DONE, 0 },
        { PEMU_SC_RET_ERRNO | 0xffff, PEMU_SC_DONE, -4095 }, { PEMU_SC_RET_TRAP | 0x1234, PEMU_SC_TRAP, 0x1234 }, { PEMU_SC_RET_KILL_THREAD, PEMU_SC_KILLT, SIGSYS },
        { PEMU_SC_RET_KILL_PROCESS, PEMU_SC_KILLP, SIGSYS }, { PEMU_SC_RET_TRACE | 5, PEMU_SC_DONE, -ENOSYS }, { PEMU_SC_RET_USER_NOTIF, PEMU_SC_DONE, -ENOSYS },
        { 0x00200000u, PEMU_SC_KILLP, SIGSYS } /* unknown action */ };
    for (size_t i = 0; i < ARRSZ(acts); i++) {
        struct pemu_bpf f[] = { RET(acts[i].ret) };
        reset(); pemu_sc_gnnp = 1; install(f, 1, 0);
        int k = enter(5, 0, &v);
        CHECK(k == acts[i].kind && (k == PEMU_SC_RUN || v == acts[i].val), "action %#x: kind %d val %ld, want %d/%ld", acts[i].ret, k, v, acts[i].kind, acts[i].val);
    }
    /* 5. several filters: the lowest action wins; a tie goes to the newest */
    reset(); pemu_sc_gnnp = 1;
    { struct pemu_bpf a[] = { RET(PEMU_SC_RET_ERRNO | 1) }, b[] = { RET(PEMU_SC_RET_ERRNO | 2) }, c[] = { RET(PEMU_SC_RET_ALLOW) }, d[] = { RET(PEMU_SC_RET_KILL_PROCESS) };
      install(a, 1, 0); install(b, 1, 0); install(c, 1, 0);
      CHECK(enter(5, 0, &v) == PEMU_SC_DONE && v == -2, "tie between ERRNO 1 and ERRNO 2: the newest must win, got %ld", v);
      install(d, 1, 0);
      CHECK(enter(5, 0, &v) == PEMU_SC_KILLP, "KILL_PROCESS must beat ERRNO"); }
    /* 6. a runtime division by zero ends the filter with 0 (KILL_THREAD) */
    reset(); pemu_sc_gnnp = 1;
    { struct pemu_bpf f[] = { I(PB_LDX | PB_IMM, 0, 0, 0), I(PB_LD | PB_IMM, 0, 0, 9), I(PB_ALU | PB_DIV | PB_X, 0, 0, 0), RET(PEMU_SC_RET_ALLOW) };
      CHECK(pemu_sc_check(f, 4) == 0 && install(f, 4, 0) == 0, "div by X is valid"); CHECK(enter(5, 0, &v) == PEMU_SC_KILLT, "A/X with X = 0"); }
    /* 7. flags and modes */
    reset(); pemu_sc_gnnp = 1;
    { struct pemu_bpf f[] = { RET(PEMU_SC_RET_ALLOW) };
      CHECK(install(f, 1, 8) == -EINVAL, "NEW_LISTENER"); CHECK(install(f, 1, 32) == -EINVAL, "WAIT_KILLABLE_RECV");
      CHECK(install(f, 1, 0x100) == -EINVAL, "unknown flag"); CHECK(install(f, 1, 16) == -EINVAL, "TSYNC_ESRCH without TSYNC");
      CHECK(install(f, 1, 2 | 4) == 0, "LOG | SPEC_ALLOW"); CHECK(install(NULL, 1, 0) == -EFAULT, "NULL program");
      uint32_t act = PEMU_SC_RET_ALLOW, bad = 0x12340000u;
      CHECK(pemu_sc_seccomp(2, 0, &act) == 0, "GET_ACTION_AVAIL ALLOW"); act = PEMU_SC_RET_USER_NOTIF; CHECK(pemu_sc_seccomp(2, 0, &act) == -EOPNOTSUPP, "USER_NOTIF is not available");
      CHECK(pemu_sc_seccomp(2, 0, &bad) == -EOPNOTSUPP, "unknown action"); CHECK(pemu_sc_seccomp(3, 0, NULL) == -EINVAL, "GET_NOTIF_SIZES"); CHECK(pemu_sc_seccomp(99, 0, NULL) == -EINVAL, "unknown op");
      CHECK(pemu_sc_seccomp(0, 0, NULL) == -EINVAL, "strict mode after a filter"); }
    reset();
    CHECK(pemu_sc_seccomp(0, 1, NULL) == -EINVAL && pemu_sc_seccomp(0, 0, (void *) 1) == -EINVAL, "strict: flags and args must be 0");
    CHECK(pemu_sc_seccomp(0, 0, NULL) == 0 && pemu_sc_mode() == 1, "strict mode");
    CHECK(enter(0, 0, &v) == PEMU_SC_RUN && enter(1, 0, &v) == PEMU_SC_RUN && enter(60, 0, &v) == PEMU_SC_RUN && enter(15, 0, &v) == PEMU_SC_RUN, "strict allows read write exit rt_sigreturn");
    CHECK(enter(39, 0, &v) == PEMU_SC_KILLP && v == SIGKILL, "strict kills with SIGKILL"); CHECK(enter(231, 0, &v) == PEMU_SC_KILLP, "strict: exit_group is not allowed (as the kernel)");
    { struct pemu_bpf f[] = { RET(PEMU_SC_RET_ALLOW) }; pemu_sc_gnnp = 1; CHECK(install(f, 1, 0) == -EINVAL, "a filter after strict mode"); }
    /* fake root may install without no_new_privs */
    reset(); pemu_fakeroot = 1; pemu_uid[PEMU_E] = 0;
    { struct pemu_bpf f[] = { RET(PEMU_SC_RET_ALLOW) }; CHECK(install(f, 1, 0) == 0, "fake root installs without no_new_privs"); }
    pemu_uid[PEMU_E] = 1000; { struct pemu_bpf f[] = { RET(PEMU_SC_RET_ALLOW) }; reset(); CHECK(install(f, 1, 0) == -EACCES, "fake uid 1000 needs no_new_privs"); } pemu_fakeroot = 0;
    /* 8. TSYNC: a process-wide filter that a thread created before it also runs; its tie rank is by sequence number */
    reset(); pemu_sc_gnnp = 0;
    { struct pemu_bpf f[] = { LD_NR, JEQ(39, 0, 1), RET(PEMU_SC_RET_ERRNO | 9), RET(PEMU_SC_RET_ALLOW) };
      pemu_sct.nnp = 1;
      CHECK(install(f, ARRSZ(f), 1) == 0, "TSYNC install");
      struct pemu_sc_task other; memset(&other, 0, sizeof other);       /* another thread: no filter of its own */
      pemu_sct = other; CHECK(enter(39, 0, &v) == PEMU_SC_DONE && v == -9, "the other thread must run the TSYNC filter");
      CHECK(pemu_sc_nnp(), "TSYNC sets no_new_privs for every thread"); }

    /* 9. exec hand-over: export, forget everything, import, same behaviour */
    reset(); pemu_sc_gnnp = 1;
    { struct pemu_bpf a[] = { LD_NR, JEQ(39, 0, 1), RET(PEMU_SC_RET_ERRNO | 11), RET(PEMU_SC_RET_ALLOW) }, b[] = { LD_NR, JEQ(39, 0, 1), RET(PEMU_SC_RET_ERRNO | 12), RET(PEMU_SC_RET_ALLOW) };
      install(a, 4, 0); install(b, 4, 1);                                  /* one own, one TSYNC: both must come across, with the tie order */
      int fd = -1; long e = pemu_sc_export(&fd);
      CHECK(e == 0 && fd >= 3, "export: %ld fd %d", e, fd);
      CHECK(fd >= 0 && write(fd, "x", 1) < 0, "the hand-over memfd must be sealed against writes");
      long before = enter(39, 0, &v), vb = v;
      reset(); CHECK(!PEMU_SC_ACTIVE(), "reset");
      pemu_sc_fd = fd; CHECK(pemu_sc_import() == 0, "import");
      CHECK(PEMU_SC_ACTIVE() && pemu_sc_nnp(), "after import: filters and no_new_privs");
      CHECK(enter(39, 0, &v) == before && v == vb, "same verdict after the exec: %ld vs %ld", v, vb);
      CHECK(enter(40, 0, &v) == PEMU_SC_RUN, "an allowed call"); }
    /* nothing to hand over: no fd */
    reset(); { int fd = -2; CHECK(pemu_sc_export(&fd) == 0 && fd == -1, "no filters, no flags: nothing to hand over"); }
    reset(); pemu_sct.strict = 1; { int fd = -2; CHECK(pemu_sc_export(&fd) == 0 && fd >= 3, "strict mode is handed over"); pemu_sc_fd = fd; reset(); CHECK(pemu_sc_import() == 0 && pemu_sc_mode() == 1, "strict mode after the exec"); }
    /* a damaged hand-over fails closed */
    reset(); { int p[2]; pipe(p); write(p[1], "garbage-garbage-garbage", 23); close(p[1]); pemu_sc_fd = p[0]; CHECK(pemu_sc_import() == -1, "garbage hand-over must be refused"); }
    reset(); { int p[2]; pipe(p); close(p[1]); pemu_sc_fd = p[0]; CHECK(pemu_sc_import() == -1, "an empty hand-over (the fd was drained) must be refused"); }

    /* 10. the TRAP siginfo, byte for byte as Linux lays it out */
    { siginfo_t si; pemu_sc_siginfo(&si, 0x42, 102, 0xdeadbeef); uint8_t *p = (uint8_t *) &si;
      CHECK(*(int32_t *) p == SIGSYS && *(int32_t *) (p + 4) == 0x42 && *(int32_t *) (p + 8) == 1 && *(uint64_t *) (p + 16) == 0xdeadbeef && *(int32_t *) (p + 24) == 102 && *(uint32_t *) (p + 28) == 0xC000003Eu, "siginfo layout"); }

    if (fails) { printf("test_seccomp: %d FAILED\n", fails); return 1; }
    printf("test_seccomp: ok\n");
    return 0;
}

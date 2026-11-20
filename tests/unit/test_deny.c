/* tests/unit/test_deny.c - the single syscall refusal table (src/proot/deny.inc.h) of the x86_64 guest hook.
 * Host-native, no guest binaries.  hs() is the test's own and NEVER reaches the kernel: it only counts calls, so a refused call is shown
 * to be refused before the host is touched (nothing here can run move_mount, unshare, ptrace ... on this machine). */
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
#include <sys/syscall.h>
static int hs_calls;
static long hs(long nr, long a, long b, long c_, long d, long e, long f) { (void) nr; (void) a; (void) b; (void) c_; (void) d; (void) e; (void) f; hs_calls++; return -ENOSYS; }
#include "pemu_root.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

static int policy(uint64_t n, uint64_t a0, long *r) { *r = 0; return pemu_policy(n, a0, 0, 0, 0, 0, r); }
static const struct pemu_deny *row(const char *name) {
    for (size_t i = 0; i < sizeof pemu_deny_tab / sizeof *pemu_deny_tab; i++) if (!strcmp(pemu_deny_tab[i].name, name)) return &pemu_deny_tab[i];
    return NULL;
}
#define NROWS (sizeof pemu_deny_tab / sizeof *pemu_deny_tab)

int main(void) {
    long r;
    pemu_on = 0;
    /* 1. the x86_64 numbers the task names, literally */
    static const struct { const char *name; int x64; } want[] = {
        {"ptrace", 101}, {"setns", 308}, {"unshare", 272}, {"open_by_handle_at", 304}, {"name_to_handle_at", 303},
        {"open_tree", 428}, {"move_mount", 429}, {"fsopen", 430}, {"fsconfig", 431}, {"fsmount", 432},
        {"fspick", 433}, {"mount_setattr", 442}, {"open_tree_attr", 467},
        {"process_vm_readv", 310}, {"process_vm_writev", 311}, {"kcmp", 312}, {"pidfd_getfd", 438}, {"bpf", 321}, {"perf_event_open", 298}, {"userfaultfd", 323}, {"landlock_create_ruleset", 444}, {"landlock_add_rule", 445}, {"landlock_restrict_self", 446}, {"statmount", 457}, {"listmount", 458}, {"keyctl", 250}, {"add_key", 248}, {"request_key", 249},
        {"sethostname", 170}, {"setdomainname", 171}, {"settimeofday", 164}, {"clock_settime", 227}, {"clock_adjtime", 305}, {"adjtimex", 159}, {"syslog", 103}, {"vhangup", 153}, {"quotactl_fd", 443}, {"lsm_set_self_attr", 460}, {"migrate_pages", 256}, {"move_pages", 279}, {"process_madvise", 440},
    };
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        const struct pemu_deny *d = row(want[i].name);
        CHECK(d != NULL, "%s is missing from the table", want[i].name);
        if (d) CHECK(d->nr == want[i].x64, "%s: number %d, want %d", want[i].name, d->nr, want[i].x64);
    }
    /* 2. on an x86_64 host the table agrees with the host's own numbers (the table holds x86_64 numbers; on an aarch64 host SYS_* are another ABI, so there is nothing to compare) */
#if defined(__x86_64__)
#define HOSTCHK(n) do { const struct pemu_deny *d = row(#n); if (d) CHECK(d->nr == SYS_##n, "%s: table has %d, SYS_%s is %d", #n, d->nr, #n, (int) SYS_##n); } while (0)
    HOSTCHK(ptrace); HOSTCHK(setns); HOSTCHK(unshare); HOSTCHK(open_by_handle_at); HOSTCHK(name_to_handle_at);
#ifdef SYS_open_tree
    HOSTCHK(open_tree);
#endif
#ifdef SYS_move_mount
    HOSTCHK(move_mount);
#endif
#ifdef SYS_fsopen
    HOSTCHK(fsopen);
#endif
#ifdef SYS_fsconfig
    HOSTCHK(fsconfig);
#endif
#ifdef SYS_fsmount
    HOSTCHK(fsmount);
#endif
#ifdef SYS_fspick
    HOSTCHK(fspick);
#endif
#ifdef SYS_mount_setattr
    HOSTCHK(mount_setattr);
#endif
#ifdef SYS_open_tree_attr
    HOSTCHK(open_tree_attr);
#endif
#ifdef SYS_process_vm_readv
    HOSTCHK(process_vm_readv);
#endif
#ifdef SYS_process_vm_writev
    HOSTCHK(process_vm_writev);
#endif
#ifdef SYS_kcmp
    HOSTCHK(kcmp);
#endif
#ifdef SYS_pidfd_getfd
    HOSTCHK(pidfd_getfd);
#endif
#ifdef SYS_bpf
    HOSTCHK(bpf);
#endif
#ifdef SYS_perf_event_open
    HOSTCHK(perf_event_open);
#endif
#ifdef SYS_userfaultfd
    HOSTCHK(userfaultfd);
#endif
#ifdef SYS_landlock_create_ruleset
    HOSTCHK(landlock_create_ruleset);
#endif
#ifdef SYS_landlock_add_rule
    HOSTCHK(landlock_add_rule);
#endif
#ifdef SYS_landlock_restrict_self
    HOSTCHK(landlock_restrict_self);
#endif
#ifdef SYS_statmount
    HOSTCHK(statmount);
#endif
#ifdef SYS_listmount
    HOSTCHK(listmount);
#endif
#ifdef SYS_keyctl
    HOSTCHK(keyctl);
#endif
#ifdef SYS_add_key
    HOSTCHK(add_key);
#endif
#ifdef SYS_request_key
    HOSTCHK(request_key);
#endif
#ifdef SYS_sethostname
    HOSTCHK(sethostname);
#endif
#ifdef SYS_setdomainname
    HOSTCHK(setdomainname);
#endif
#ifdef SYS_settimeofday
    HOSTCHK(settimeofday);
#endif
#ifdef SYS_clock_settime
    HOSTCHK(clock_settime);
#endif
#ifdef SYS_clock_adjtime
    HOSTCHK(clock_adjtime);
#endif
#ifdef SYS_adjtimex
    HOSTCHK(adjtimex);
#endif
#ifdef SYS_syslog
    HOSTCHK(syslog);
#endif
#ifdef SYS_vhangup
    HOSTCHK(vhangup);
#endif
#ifdef SYS_quotactl_fd
    HOSTCHK(quotactl_fd);
#endif
#ifdef SYS_lsm_set_self_attr
    HOSTCHK(lsm_set_self_attr);
#endif
#ifdef SYS_migrate_pages
    HOSTCHK(migrate_pages);
#endif
#ifdef SYS_move_pages
    HOSTCHK(move_pages);
#endif
#ifdef SYS_process_madvise
    HOSTCHK(process_madvise);
#endif
#endif
    /* 3. every row has a number, and no number is used twice */
    for (size_t i = 0; i < NROWS; i++) {
        CHECK(pemu_deny_tab[i].nr >= 0, "%s has no number", pemu_deny_tab[i].name);
        for (size_t j = i + 1; j < NROWS; j++) CHECK(pemu_deny_tab[i].nr != pemu_deny_tab[j].nr, "%s and %s share number %d", pemu_deny_tab[i].name, pemu_deny_tab[j].name, pemu_deny_tab[i].nr);
    }
    /* 4. refused rows are EPERM and never reach the host */
    static const uint64_t flagsamples[] = {0, 1, 0x20000, 0x7e020000ULL, 0x80, 0x400, 0x200, 0x40000, 0xffffffffULL};
    for (size_t i = 0; i < NROWS; i++) {
        const struct pemu_deny *d = &pemu_deny_tab[i];
        for (size_t k = 0; k < sizeof flagsamples / sizeof *flagsamples; k++) {
            int ra; long rr;
            hs_calls = 0;
            ra = policy((uint64_t) d->nr, flagsamples[k], &rr);
            CHECK(hs_calls == 0, "%s: the policy touched the host", d->name);
            if (d->how == PEMU_DN_ALWAYS) CHECK(ra && rr == -EPERM, "%s must be EPERM (got %d/%ld)", d->name, ra, rr);
        }
    }
    /* 5. unshare: every CLONE_NEW* flag alone is EPERM; flags without one go on to the host */
    static const uint64_t newflags[] = {0x20000 /*NEWNS*/, 0x2000000 /*NEWCGROUP*/, 0x4000000 /*NEWUTS*/, 0x8000000 /*NEWIPC*/, 0x10000000 /*NEWUSER*/, 0x20000000 /*NEWPID*/, 0x40000000 /*NEWNET*/, 0x80 /*NEWTIME*/};
    static const uint64_t okflags[] = {0, 0x200 /*FS*/, 0x400 /*FILES*/, 0x40000 /*SYSVSEM*/};
    {
        uint64_t nr = (uint64_t) row("unshare")->nr;
        for (size_t k = 0; k < sizeof newflags / sizeof *newflags; k++) CHECK(policy(nr, newflags[k], &r) && r == -EPERM, "unshare(%#llx) must be EPERM", (unsigned long long) newflags[k]);
        for (size_t k = 0; k < sizeof okflags / sizeof *okflags; k++) CHECK(!policy(nr, okflags[k], &r), "unshare(%#llx) must pass through", (unsigned long long) okflags[k]);
        CHECK(policy(nr, 0x200 | 0x4000000, &r) && r == -EPERM, "unshare(FS|NEWUTS) must be EPERM");
    }
    /* 6. name_to_handle_at is not refused; without a rootfs it is left to the plain pass-through */
    CHECK(!policy(303, 0, &r), "name_to_handle_at without a rootfs must pass through");
    /* 6b. with a rootfs (pemu_on) name_to_handle_at is answered by the policy (translated), never EPERM; the host stub is reached, not a raw guest path */
    pemu_on = 1; hs_calls = 0;
    CHECK(policy(303, 0, &r) && r != -EPERM, "name_to_handle_at with a rootfs must be answered by the translation, not refused");
    pemu_on = 0; hs_calls = 0;
    /* 6c. a guest passing syscall number -1 must not match any row */
    CHECK(!policy((uint64_t) -1, 0, &r), "syscall -1 must not match a row");
    /* 6d. adjtimex / clock_adjtime: only the read-only forms reach the host, on a private copy; a bad pointer is EFAULT, never a crash */
    {
        uint64_t nra = (uint64_t) row("adjtimex")->nr, nrc = (uint64_t) row("clock_adjtime")->nr;
        static const struct { uint32_t modes; int ok; } cases[] = { {0, 1}, {0xa001, 1}, {0x100 /*ADJ_SETOFFSET*/, 0}, {1 /*ADJ_OFFSET*/, 0}, {0x8000 /*ADJ_TICK*/, 0}, {0x2001, 0}, {0xffffffffu, 0} };
        for (size_t k = 0; k < sizeof cases / sizeof *cases; k++) {
            uint64_t tx[26]; memset(tx, 0, sizeof tx); tx[0] = cases[k].modes;
            hs_calls = 0; r = 0;
            int ans = pemu_policy(nra, (uint64_t) (uintptr_t) tx, 0, 0, 0, 0, &r);
            CHECK(ans && (cases[k].ok ? (r == -ENOSYS && hs_calls == 1) : (r == -EPERM && hs_calls == 0)), "adjtimex modes=%#x: answer %d r=%ld hs=%d", cases[k].modes, ans, r, hs_calls);
            hs_calls = 0; r = 0;
            ans = pemu_policy(nrc, 0, (uint64_t) (uintptr_t) tx, 0, 0, 0, &r);
            CHECK(ans && (cases[k].ok ? (r == -ENOSYS && hs_calls == 1) : (r == -EPERM && hs_calls == 0)), "clock_adjtime modes=%#x: answer %d r=%ld hs=%d", cases[k].modes, ans, r, hs_calls);
        }
        hs_calls = 0; r = 0;
        CHECK(pemu_policy(nra, 1, 0, 0, 0, 0, &r) && r == -EFAULT && hs_calls == 0, "adjtimex(bad pointer) must be EFAULT without the host");
        CHECK(pemu_policy(nrc, 0, 0, 0, 0, 0, &r) && r == -EFAULT && hs_calls == 0, "clock_adjtime(NULL) must be EFAULT without the host");
    }
    hs_calls = 0;
    /* 6b. the bitmap in front of the table must answer exactly like the scan, for every number (and a few huge ones) */
    {
        uint64_t big[] = { 1023, 1024, 1025, 4096, (uint64_t) -1, (uint64_t) 1 << 40 };
        for (uint64_t n = 0; n < 1100; n++) CHECK(pemu_deny_find(n) == pemu_deny_find_scan(n), "nr %llu: bitmap and scan disagree", (unsigned long long) n);
        for (size_t k = 0; k < sizeof big / sizeof *big; k++) CHECK(pemu_deny_find(big[k]) == pemu_deny_find_scan(big[k]), "nr %llu (big): bitmap and scan disagree", (unsigned long long) big[k]);
    }
    /* 7. a number that is not in the table must not match: 117 is ptrace on AArch64 but setresuid on x86_64, 264 is name_to_handle_at on AArch64 but renameat here */
    CHECK(!policy(117, 0, &r), "x86_64 117 (setresuid) must not be refused");
    CHECK(!policy(264, 0, &r), "x86_64 264 (renameat) must not be refused");
    CHECK(hs_calls == 0, "the host was reached (%d calls)", hs_calls);
    if (fails) { printf("test_deny: %d FAILED\n", fails); return 1; }
    printf("test_deny: ok (%zu rows, x86_64 guest)\n", NROWS);
    return 0;
}

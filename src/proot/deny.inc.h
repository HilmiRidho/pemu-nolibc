/* src/proot/deny.inc.h - ONE syscall refusal policy. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0
 *
 * Calls that pemu cannot translate or provide, and that would otherwise act on the host or on the emulator process itself.  Each row
 * names the call once and gives its x86_64 syscall number (pemu_policy looks it up here).  Numbers are GUEST numbers (they are never passed to the host).
 *
 *   PEMU_DN_ALWAYS     -EPERM always (as a container whose seccomp profile blocks the call).
 *   PEMU_DN_NEWNS      -EPERM when the flags in arg 0 ask for a new namespace (unshare); other flags pass through to the host.
 *   PEMU_DN_TRANSLATE  not refused: with a rootfs the path is translated (name_to_handle_at); without one it passes through unchanged.
 *   PEMU_DN_TIMEX0/1   adjtimex(buf) / clock_adjtime(clk, buf): only the READ-ONLY forms go to the host (modes == 0, or ADJ_OFFSET_SS_READ),
 *                      any request to change the clock is -EPERM.  The struct is copied once (a guest thread must not turn modes into a
 *                      write after the check) and the host gets the private copy; the result is copied back.
 *
 * The policy is applied with or without a rootfs, like the ptrace refusal it replaces: unshare / setns / the mount API would change the
 * emulator process or the host mount tree either way. */
enum { PEMU_DN_ALWAYS, PEMU_DN_NEWNS, PEMU_DN_TRANSLATE, PEMU_DN_TIMEX0, PEMU_DN_TIMEX1 };

/* unshare(2) knows CLONE_NEWTIME (0x80) as well; clone(2) has no such flag (that bit is the exit signal), see PEMU_CLONE_NEWMASK */
#define PEMU_UNSHARE_NEWMASK (PEMU_CLONE_NEWMASK | 0x80ULL)

struct pemu_deny { const char *name; int nr; int how; };         /* nr: x86_64 syscall number */
static const struct pemu_deny pemu_deny_tab[] = {
    /* name                     x86_64 number */
    { "ptrace",                 101,  PEMU_DN_ALWAYS },         /* the host would trace the emulator (host registers, no guest view) */
    { "setns",                  308,  PEMU_DN_ALWAYS },         /* joining another namespace would leave the emulated view */
    { "unshare",                272,  PEMU_DN_NEWNS },          /* new namespaces cannot be offered (see clone) and would change the host process */
    { "open_by_handle_at",      304,  PEMU_DN_ALWAYS },         /* a handle names an inode, not a rootfs path (the "shocker" escape) */
    { "name_to_handle_at",      303,  PEMU_DN_TRANSLATE },      /* takes a path: resolved in the rootfs */
    /* new mount API: it would mount / move / reconfigure on the host, bypassing the bind table */
    { "open_tree",              428,  PEMU_DN_ALWAYS },
    { "move_mount",             429,  PEMU_DN_ALWAYS },
    { "fsopen",                 430,  PEMU_DN_ALWAYS },
    { "fsconfig",               431,  PEMU_DN_ALWAYS },
    { "fsmount",                432,  PEMU_DN_ALWAYS },
    { "fspick",                 433,  PEMU_DN_ALWAYS },
    { "mount_setattr",          442,  PEMU_DN_ALWAYS },
    { "open_tree_attr",         467,  PEMU_DN_ALWAYS },
    /* acting on other host processes, host kernel state or the host's mounts / keyrings: refused like a container's default seccomp profile does */
    { "process_vm_readv",       310,  PEMU_DN_ALWAYS },         /* reads another process's memory by host pid (the emulator's own, or any process of this user) */
    { "process_vm_writev",      311,  PEMU_DN_ALWAYS },         /* writes another process's memory by host pid (the emulator's own, or any process of this user) */
    { "kcmp",                   312,  PEMU_DN_ALWAYS },         /* compares kernel objects of host pids */
    { "pidfd_getfd",            438,  PEMU_DN_ALWAYS },         /* steals an fd from another host process, which may be outside the rootfs */
    { "bpf",                    321,  PEMU_DN_ALWAYS },         /* loads programs into the host kernel (a default container seccomp profile blocks it) */
    { "perf_event_open",        298,  PEMU_DN_ALWAYS },         /* counters / tracing of host tasks (blocked by a default container seccomp profile) */
    { "userfaultfd",            323,  PEMU_DN_ALWAYS },         /* as blocked by a default container seccomp profile */
    { "landlock_create_ruleset",444,  PEMU_DN_ALWAYS },         /* Landlock would restrict the emulator itself, whose host paths are not the guest's; programs fall back to no sandbox */
    { "landlock_add_rule",      445,  PEMU_DN_ALWAYS },
    { "landlock_restrict_self", 446,  PEMU_DN_ALWAYS },
    { "statmount",              457,  PEMU_DN_ALWAYS },         /* reports the HOST mount table, not pemu's bind table */
    { "listmount",              458,  PEMU_DN_ALWAYS },         /* lists the HOST mounts, not pemu's bind table */
    { "keyctl",                 250,  PEMU_DN_ALWAYS },         /* the host's session / user keyrings are shared with every process of this user */
    { "add_key",                248,  PEMU_DN_ALWAYS },         /* same keyrings as keyctl */
    { "request_key",            249,  PEMU_DN_ALWAYS },         /* same keyrings as keyctl */
    /* act on the host's clock, name, kernel log or on other host processes (only effective when pemu runs as real root, where they would hit the host) */
    { "sethostname",            170,  PEMU_DN_ALWAYS },         /* would rename the HOST when pemu runs as real root */
    { "setdomainname",          171,  PEMU_DN_ALWAYS },         /* same, the NIS domain name */
    { "settimeofday",           164,  PEMU_DN_ALWAYS },         /* would set the HOST clock when pemu runs as real root */
    { "clock_settime",          227,  PEMU_DN_ALWAYS },         /* same */
    { "clock_adjtime",          305,  PEMU_DN_TIMEX1 },         /* changing the HOST clock: refused; reading it (modes 0) is not */
    { "adjtimex",               159,  PEMU_DN_TIMEX0 },
    { "syslog",                 103,  PEMU_DN_ALWAYS },         /* reads / clears the HOST kernel log */
    { "vhangup",                153,  PEMU_DN_ALWAYS },         /* hangs up the emulator's controlling terminal */
    { "quotactl_fd",            443,  PEMU_DN_ALWAYS },         /* host filesystem quotas */
    { "lsm_set_self_attr",      460,  PEMU_DN_ALWAYS },         /* would change the emulator's own LSM context */
    { "migrate_pages",          256,  PEMU_DN_ALWAYS },         /* moves the pages of another host process */
    { "move_pages",             279,  PEMU_DN_ALWAYS },         /* moves the pages of another host process */
    { "process_madvise",        440,  PEMU_DN_ALWAYS },         /* advises the memory of another host process (pidfd) */
};

/* Every guest syscall asks this first, and almost none is in the table: a bitmap of the numbers that are in it answers "no" with one load
 * instead of a scan of every row.  Built under pthread_once, so a thread that arrives while it is being built waits for it (it never sees
 * half of it: the policy must not fail open).  Numbers >= PEMU_DENY_BM still take the scan. */
#define PEMU_DENY_BM 1024
static uint8_t pemu_deny_bm[PEMU_DENY_BM / 8];
static volatile int pemu_deny_st;                          /* 0 = not built, 1 = being built, 2 = ready */
static void pemu_deny_build(void) {
    for (size_t i = 0; i < sizeof pemu_deny_tab / sizeof *pemu_deny_tab; i++)
        { int v = pemu_deny_tab[i].nr; if (v >= 0 && v < PEMU_DENY_BM) pemu_deny_bm[v >> 3] |= (uint8_t) (1u << (v & 7)); }
}
static inline void pemu_deny_ready(void) {                 /* no pthread here: pemu is not linked with it.  A thread cannot exist before the first guest syscall (clone) went through here */
    if (__builtin_expect(__atomic_load_n(&pemu_deny_st, __ATOMIC_ACQUIRE) == 2, 1)) return;
    int z = 0;
    if (__atomic_compare_exchange_n(&pemu_deny_st, &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { pemu_deny_build(); __atomic_store_n(&pemu_deny_st, 2, __ATOMIC_RELEASE); return; }
    while (__atomic_load_n(&pemu_deny_st, __ATOMIC_ACQUIRE) != 2) sched_yield();
}
static const struct pemu_deny *pemu_deny_find_scan(uint64_t n) {
    for (size_t i = 0; i < sizeof pemu_deny_tab / sizeof *pemu_deny_tab; i++)
        if (pemu_deny_tab[i].nr >= 0 && (uint64_t) pemu_deny_tab[i].nr == n) return &pemu_deny_tab[i];   /* a guest that passes -1 must not match a row */
    return NULL;
}
static const struct pemu_deny *pemu_deny_find(uint64_t n) {
    pemu_deny_ready();
    if (n < PEMU_DENY_BM && !((pemu_deny_bm[n >> 3] >> (n & 7)) & 1u)) return NULL;
    return pemu_deny_find_scan(n);
}

#define PEMU_TIMEX_SIZE 208          /* struct timex, the same 64-bit layout on x86_64 and aarch64 hosts */
#define PEMU_TIMEX_RO   0xa001u      /* ADJ_OFFSET_SS_READ: the other read-only request */
/* Copy len bytes between guest memory and a private buffer WITHOUT faulting if the guest pointer is bad (a pipe write / read reports EFAULT). */
static int pemu_guest_copy(int to_guest, void *guest, void *priv, size_t len) {
    int p[2]; int rc = 0;
    if (pipe(p) < 0) return -EMFILE;
    if (to_guest) { if (host_write(p[1], priv, len) != (ssize_t) len || host_read(p[0], guest, len) != (ssize_t) len) rc = -EFAULT; }
    else          { if (host_write(p[1], guest, len) != (ssize_t) len || host_read(p[0], priv, len) != (ssize_t) len) rc = -EFAULT; }
    host_close(p[0]); host_close(p[1]);
    return rc;
}
/* adjtimex(buf) (clk_arg == 0) or clock_adjtime(clk, buf) (clk_arg == 1) */
static long pemu_timex(int clock_variant, uint64_t clk, uint64_t buf) {
    uint64_t tx[PEMU_TIMEX_SIZE / 8];
    long r = pemu_guest_copy(0, (void*) buf, tx, sizeof tx);
    if (r < 0) return r;
    uint32_t modes = (uint32_t) tx[0];
    if (modes != 0 && modes != PEMU_TIMEX_RO) return -EPERM;
#if defined(HOP_adjtimex) && defined(HOP_clock_adjtime) && __SIZEOF_LONG__ == 8      /* a 32-bit host's struct timex is smaller and laid out differently */
    r = clock_variant ? hs(HOP_clock_adjtime, (long) clk, (long) tx, 0, 0, 0, 0) : hs(HOP_adjtimex, (long) tx, 0, 0, 0, 0, 0);
    if (pemu_guest_copy(1, (void*) buf, tx, sizeof tx) < 0) return -EFAULT;
    return r;
#else
    (void) clk; (void) clock_variant;
    return -ENOSYS;
#endif
}

/* Pure decision for a row that was already found (d may be NULL), no side effects: 1 = refuse, 0 = let the call go on. */
static int pemu_deny_row_refuses(const struct pemu_deny *d, uint64_t a0) {
    if (!d) return 0;
    if (d->how == PEMU_DN_ALWAYS) return 1;
    if (d->how == PEMU_DN_NEWNS) return (a0 & PEMU_UNSHARE_NEWMASK) != 0;
    return 0;
}

/* name_to_handle_at(dirfd, path, handle, mount_id, flags): the path is a guest path.  AT_SYMLINK_FOLLOW is 0x400. */
static long pemu_name_to_handle_at(uint64_t dirfd, const char *path, uint64_t handle, uint64_t mount_id, uint64_t flags) {
#ifdef HOP_name_to_handle_at
    return pemu_callat(HOP_name_to_handle_at, (long) dirfd, path, !!(flags & 0x400), (long) handle, (long) mount_id, (long) flags, 0);
#else
    (void) dirfd; (void) path; (void) handle; (void) mount_id; (void) flags;
    return -ENOSYS;
#endif
}

/* The syscall hook's single entry point.  Returns 1 and sets *res when the policy answered the call, 0 to let the normal translation run. */
static int pemu_policy(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, long *res) {
    const struct pemu_deny *d = pemu_deny_find(n);
    if (!d) return 0;
    if (pemu_deny_row_refuses(d, a0)) { plog_refuse(d->how == PEMU_DN_NEWNS ? "new namespace refused (EPERM)" : "refused on purpose (EPERM): pemu cannot provide it and it would act on the host or on the emulator itself"); *res = -EPERM; return 1; }
    if (d->how == PEMU_DN_TIMEX0) { *res = pemu_timex(0, 0, a0); return 1; }
    if (d->how == PEMU_DN_TIMEX1) { *res = pemu_timex(1, a0, a1); return 1; }
    if (d->how == PEMU_DN_TRANSLATE && pemu_on) { *res = pemu_name_to_handle_at(a0, (const char*) a1, a2, a3, a4); return 1; }
    return 0;
}

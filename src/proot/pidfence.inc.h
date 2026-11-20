/* src/proot/pidfence.inc.h - the guest may only name processes of its own tree; POSIX message queue names.
 * Fragment of pemu_root.h (not self-contained; needs hs(), pemu_on, pemu_root_get, pemu_guest_copy). SPDX-License-Identifier: Apache-2.0
 *
 * A guest process is a plain host process, so a syscall that carries a PID used to act on whatever host process had that number:
 * kill(host_shell_pid, SIGKILL), pidfd_open, sched_setaffinity, setpriority(PRIO_USER) ... and kill(0, sig) / kill(-1, sig) reached every
 * process of the caller's process group or of the user, host processes included.  This table is the one place that decides, for both guest
 * ABIs (like deny.inc.h):
 *
 *   PEMU_F_PID0    a0 is a process or thread id; a process outside the tree answers ESRCH (0 = the caller, never refused).
 *   PEMU_F_PIDS2   a0 = tgid, a1 = tid (tgkill, rt_tgsigqueueinfo): both must be in the tree.
 *   PEMU_F_KILL    kill(pid, sig): pid > 0 as PEMU_F_PID0; 0 / < -1 / -1 are expanded here to the processes of the tree only.
 *   PEMU_F_SETPRIO / PEMU_F_GETPRIO   set/getpriority(which, who): PRIO_PROCESS as PEMU_F_PID0; PRIO_PGRP / PRIO_USER only touch processes of the tree.
 *   PEMU_F_IOPRIO  ioprio_get/set: IOPRIO_WHO_PROCESS as PEMU_F_PID0; the group / user forms are EPERM (they cannot be limited).
 *   PEMU_F_CAPGET  capget(hdr, data): hdr->pid is checked.
 *   PEMU_F_MQOPEN / PEMU_F_MQUNLINK   mq_open / mq_unlink: the queue name lives in the HOST's mqueue namespace; inside a rootfs it is prefixed with an id
 *                  of the rootfs, so two rootfs (and the host) do not see, open or unlink each other's queues.
 *
 * "The tree" = the monitor process (pemu_fence_root, the launching pemu) and every descendant of it.  The monitor is the child subreaper
 * (pemu_start_monitor), so an orphaned guest process is re-parented to it and stays in the tree.  Membership is read from the parent
 * links in /proc/<pid>/stat.  This is NOT PID virtualisation: the guest still sees host PIDs and /proc is still the host's (see README,
 * "Limitations"); it only stops the guest from acting on a process it did not start.
 *
 * Decided once by the FIRST pemu and handed to re-exec'd ones as the hidden `--fence-root PID`: the environment is under the guest's
 * control (a guest could set PEMU_PID_FENCE=0 before an exec), an option is not.  Off (PEMU_PID_FENCE=0), or without a rootfs, or when
 * /proc cannot be read: pemu_fence_root stays 0 and nothing here changes behaviour. */
#define PEMU_F_PID0   0
#define PEMU_F_PIDS2  1
#define PEMU_F_KILL   2
#define PEMU_F_SETPRIO 3
#define PEMU_F_GETPRIO 4
#define PEMU_F_IOPRIO 5
#define PEMU_F_CAPGET 6
#define PEMU_F_MQOPEN 7
#define PEMU_F_MQUNLINK 8

static pid_t pemu_fence_root;                              /* 0 = fence off */

struct pemu_fence_row { const char *name; int nr; int how; };   /* nr: x86_64 syscall number */
static const struct pemu_fence_row pemu_fence_tab[] = {
    /* name                    x86_64 number */
    { "kill",                   62,    PEMU_F_KILL },
    { "tkill",                  200,   PEMU_F_PID0 },
    { "tgkill",                 234,   PEMU_F_PIDS2 },
    { "rt_sigqueueinfo",        129,   PEMU_F_PID0 },
    { "rt_tgsigqueueinfo",      297,   PEMU_F_PIDS2 },
    { "pidfd_open",             434,   PEMU_F_PID0 },
    { "setpriority",            141,   PEMU_F_SETPRIO },
    { "getpriority",            140,   PEMU_F_GETPRIO },
    { "sched_setparam",         142,   PEMU_F_PID0 },
    { "sched_getparam",         143,   PEMU_F_PID0 },
    { "sched_setscheduler",     144,   PEMU_F_PID0 },
    { "sched_getscheduler",     145,   PEMU_F_PID0 },
    { "sched_rr_get_interval",  148,   PEMU_F_PID0 },
    { "sched_setaffinity",      203,   PEMU_F_PID0 },
    { "sched_getaffinity",      204,   PEMU_F_PID0 },
    { "sched_setattr",          314,   PEMU_F_PID0 },
    { "sched_getattr",          315,   PEMU_F_PID0 },
    { "ioprio_set",             251,   PEMU_F_IOPRIO },
    { "ioprio_get",             252,   PEMU_F_IOPRIO },
    { "getpgid",                121,   PEMU_F_PID0 },
    { "getsid",                 124,   PEMU_F_PID0 },
    { "setpgid",                109,   PEMU_F_PID0 },
    { "prlimit64",              302,   PEMU_F_PID0 },
    { "capget",                 125,   PEMU_F_CAPGET },
    { "get_robust_list",        274,   PEMU_F_PID0 },
    { "mq_open",                240,   PEMU_F_MQOPEN },
    { "mq_unlink",              241,   PEMU_F_MQUNLINK },
};
#define PEMU_FENCE_BM 1024
static uint8_t pemu_fence_bm[PEMU_FENCE_BM / 8];
static volatile int pemu_fence_st;                         /* 0 = not built, 1 = being built, 2 = ready (same scheme as pemu_deny_bm) */
static void pemu_fence_ready(void) {
    if (__builtin_expect(__atomic_load_n(&pemu_fence_st, __ATOMIC_ACQUIRE) == 2, 1)) return;
    int z = 0;
    if (__atomic_compare_exchange_n(&pemu_fence_st, &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        for (size_t i = 0; i < sizeof pemu_fence_tab / sizeof *pemu_fence_tab; i++)
            { int v = pemu_fence_tab[i].nr; if (v >= 0 && v < PEMU_FENCE_BM) pemu_fence_bm[v >> 3] |= (uint8_t) (1u << (v & 7)); }
        __atomic_store_n(&pemu_fence_st, 2, __ATOMIC_RELEASE); return;
    }
    while (__atomic_load_n(&pemu_fence_st, __ATOMIC_ACQUIRE) != 2) sched_yield();
}
static const struct pemu_fence_row *pemu_fence_find(uint64_t n) {
    pemu_fence_ready();
    if (n >= PEMU_FENCE_BM || !((pemu_fence_bm[n >> 3] >> (n & 7)) & 1u)) return NULL;
    for (size_t i = 0; i < sizeof pemu_fence_tab / sizeof *pemu_fence_tab; i++)
        if (pemu_fence_tab[i].nr >= 0 && (uint64_t) pemu_fence_tab[i].nr == n) return &pemu_fence_tab[i];
    return NULL;
}

/* parent and process group of a pid (a tid works too: it shows the process's), from /proc/<pid>/stat.  -1: not readable / not there. */
static int pemu_stat_ids(pid_t p, int *ppid, int *pgrp) {
    char path[40], buf[1024]; int fd, pp = 0, pg = 0; char st; ssize_t n;
    snprintf(path, sizeof path, "/proc/%d/stat", (int) p);
    fd = host_open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    n = host_read(fd, buf, sizeof buf - 1); host_close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    char *e = strrchr(buf, ')');                           /* the command name may hold spaces and ')' */
    if (!e || sscanf(e + 1, " %c %d %d", &st, &pp, &pg) != 3) return -1;
    if (ppid) *ppid = pp;
    if (pgrp) *pgrp = pg;
    return 0;
}
/* 1 when p is the monitor or one of its descendants; fence off: always 1.
 * The answer used to cost an open + read + close + parse of /proc/<pid>/stat per ancestor on EVERY tgkill / kill / sched_* call (tgkill checks two ids).
 * Wine does these all the time (pthread_kill for suspend and abort, wineserver's kill for APCs and timers, thread priorities), so:
 *  - the process itself (and its own threads, for tgkill) is in the tree by definition: no /proc at all;
 *  - an id that was found in the tree is remembered for PEMU_OWN_TTL_MS. Only positives are kept. Pids are handed out round-robin, so one cannot come
 *    back for a different process within a quarter of a second (that takes a whole pid space of forks); the old check had the same kind of race between
 *    the check and the syscall (see docs/REVIEW.md). Entry = pid | expiry << 32 in one word, so a racing reader never pairs a pid with another's expiry. */
#define PEMU_OWN_TTL_MS 250u
#define PEMU_OWN_N 64
static uint64_t pemu_own_cache[PEMU_OWN_N];
static inline uint32_t pemu_own_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t) ((uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u); }
static int pemu_pid_owned(pid_t p) {
    if (pemu_fence_root <= 0) return 1;
    if (p <= 1) return 0;
    if (p == pemu_fence_root || p == host_getpid()) return 1;
    uint32_t now = pemu_own_now(), slot = (uint32_t) p % PEMU_OWN_N;
    uint64_t e = __atomic_load_n(&pemu_own_cache[slot], __ATOMIC_RELAXED);
    if ((uint32_t) e == (uint32_t) p && (int32_t) ((uint32_t) (e >> 32) - now) > 0) return 1;
    pid_t q = p;
    for (int d = 0; d < 256 && q > 1; d++) {
        int pp;
        if (q == pemu_fence_root) {
            __atomic_store_n(&pemu_own_cache[slot], (uint64_t) (uint32_t) p | (uint64_t) (now + PEMU_OWN_TTL_MS) << 32, __ATOMIC_RELAXED);
            return 1;
        }
        if (pemu_stat_ids(q, &pp, NULL) < 0) return 0;     /* gone, or not readable: not ours (fail closed) */
        q = (pid_t) pp;
    }
    return 0;
}
/* Call cb(pid, pgrp, arg) for every guest process of the tree, the monitor NOT included (it is pemu itself: it forwards TERM / HUP / USR1 /
 * USR2 to the first guest process, so a group or broadcast signal that also hit it would be delivered twice).  cb returns non-zero to stop. */
static void pemu_each_owned(int (*cb)(pid_t, int, void *), void *arg) {
    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        char *end; long v = strtol(e->d_name, &end, 10);
        if (*end || v <= 1 || v > 0x7fffffffL || (pid_t) v == pemu_fence_root) continue;
        int pg;
        if (pemu_stat_ids((pid_t) v, NULL, &pg) < 0 || !pemu_pid_owned((pid_t) v)) continue;
        if (cb((pid_t) v, pg, arg)) break;
    }
    closedir(d);
}

struct pemu_kctx { int sig, pgrp, all, hit, err; pid_t self; };
static int pemu_kill_cb(pid_t p, int pg, void *a) {
    struct pemu_kctx *k = a;
    if (k->all ? p == k->self : pg != k->pgrp) return 0;   /* host_kill(-1) leaves the caller out, as the kernel does */
    long r = hs(HOP_kill, (long) p, (long) k->sig, 0, 0, 0, 0);
    if (r == 0) k->hit = 1; else if (r != -ESRCH) k->err = (int) -r;
    return 0;
}
/* kill(pid, sig) for pid <= 0 */
static long pemu_fence_kill_group(int pid, int sig) {
    struct pemu_kctx k = { sig, 0, pid == -1, 0, 0, host_getpid() };
    k.pgrp = pid == 0 ? (int) getpgrp() : -pid;
    pemu_each_owned(pemu_kill_cb, &k);
    return k.hit ? 0 : k.err ? -k.err : -ESRCH;
}

struct pemu_pctx { int setp, prio, pgrp, any_pg, hit, best, err; };
static int pemu_prio_cb(pid_t p, int pg, void *a) {
    struct pemu_pctx *c = a;
    if (!c->any_pg && pg != c->pgrp) return 0;
    if (c->setp) {
        long r = hs(HOP_setpriority, 0, (long) p, (long) c->prio, 0, 0, 0);
        if (r == 0) c->hit = 1; else if (r != -ESRCH) c->err = (int) -r;
    } else {
        long r = hs(HOP_getpriority, 0, (long) p, 0, 0, 0, 0);       /* the raw syscall returns 20 - nice */
        if (r >= 0) { if (!c->hit || r > c->best) c->best = (int) r; c->hit = 1; }
    }
    return 0;
}
/* 1 = answered (*res set), 0 = go on */
static int pemu_fence_prio(int setp, uint64_t which, uint64_t who, uint64_t prio, long *res) {
    int w = (int) which;
    if (w == 0) { if ((int) who > 0 && !pemu_pid_owned((pid_t) (int) who)) { *res = -ESRCH; return 1; } return 0; }
    if (w != 1 && w != 2) return 0;                                  /* invalid: the kernel says EINVAL */
    /* PRIO_USER: the guest's uid is not the host's under a fake root, so `who` cannot be matched; every process of the tree is meant */
    struct pemu_pctx c = { setp, (int) prio, w == 1 ? ((int) who ? (int) who : (int) getpgrp()) : 0, w == 2, 0, 0, 0 };
    pemu_each_owned(pemu_prio_cb, &c);
    if (!c.hit) *res = c.err ? -c.err : -ESRCH; else *res = setp ? 0 : c.best;
    return 1;
}

/* "pemu<8 hex of the rootfs path>." in front of a queue name */
static int pemu_mq_prefix(char *out, size_t sz) {
    char rt[PATH_MAX]; uint32_t h = 2166136261u;
    pemu_root_get(rt);
    for (const char *c = rt; *c; c++) h = (h ^ (uint8_t) *c) * 16777619u;
    return snprintf(out, sz, "pemu%08x.", (unsigned) h);
}
static long pemu_fence_mq(int unlink_, const char *name, uint64_t oflag, uint64_t mode, uint64_t attr) {
    char host[256 + 16]; int pl = pemu_mq_prefix(host, sizeof host);
    size_t nl = strnlen(name, 256);
    if (nl == 0) return -EINVAL;
    if (nl + (size_t) pl >= 255) return -ENAMETOOLONG;
    if (*name == '/') { name++; nl--; }                    /* a raw syscall takes the name without its leading slash; accept both */
    memcpy(host + pl, name, nl); host[pl + nl] = 0;
#if defined(HOP_mq_open) && defined(HOP_mq_unlink)
    return unlink_ ? hs(HOP_mq_unlink, (long) host, 0, 0, 0, 0, 0)
                   : hs(HOP_mq_open, (long) host, (long) oflag, (long) mode, (long) attr, 0, 0);
#else
    (void) unlink_; (void) oflag; (void) mode; (void) attr; return -ENOSYS;
#endif
}

/* Pure decision for the PID rows, no side effect on the host: 1 = refuse with ESRCH, 0 = go on.  The tests call it directly. */
static int pemu_fence_pid_refused(int how, uint64_t a0, uint64_t a1) {
    if (how == PEMU_F_PID0) return (int) a0 > 0 && !pemu_pid_owned((pid_t) (int) a0);
    if (how == PEMU_F_PIDS2) {
        if ((int) a0 > 0 && pemu_fence_root > 0 && (pid_t) (int) a0 == host_getpid()) return 0;   /* tgkill(own tgid, tid): the kernel only accepts a tid of that process (else ESRCH), so it cannot name a foreign one */
        return ((int) a0 > 0 && !pemu_pid_owned((pid_t) (int) a0)) || ((int) a1 > 0 && !pemu_pid_owned((pid_t) (int) a1));
    }
    return 0;
}

/* The hooks' entry point (after pemu_policy).  1 = answered, *res set; 0 = go on with the normal translation. */
static int pemu_fence_call_(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, long *res);
static int pemu_fence_call(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, long *res) {
    int h = pemu_fence_call_(n, a0, a1, a2, a3, res);
    if (h && (*res == -ESRCH || *res == -EPERM)) plog_refuse(*res == -ESRCH ? "PID fence: process is not in the guest's own tree (ESRCH)" : "PID fence: cannot be limited to the guest's tree (EPERM)");
    return h;
}
static int pemu_fence_call_(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, long *res) {
    if (!pemu_on) return 0;
    const struct pemu_fence_row *d = pemu_fence_find(n);
    if (!d) return 0;
    if (d->how == PEMU_F_MQOPEN || d->how == PEMU_F_MQUNLINK) { *res = pemu_fence_mq(d->how == PEMU_F_MQUNLINK, (const char*) a0, a1, a2, a3); return 1; }
    if (pemu_fence_root <= 0) return 0;
    switch (d->how) {
    case PEMU_F_PID0: case PEMU_F_PIDS2:
        if (pemu_fence_pid_refused(d->how, a0, a1)) { *res = -ESRCH; return 1; }
        return 0;
    case PEMU_F_KILL: {
        int pid = (int) a0, sig = (int) a1;
        if (sig < 0 || sig > 64) { *res = -EINVAL; return 1; }
        if (pid > 0) { if (!pemu_pid_owned((pid_t) pid)) { *res = -ESRCH; return 1; } return 0; }
        *res = pemu_fence_kill_group(pid, sig); return 1; }
    case PEMU_F_SETPRIO: return pemu_fence_prio(1, a0, a1, a2, res);
    case PEMU_F_GETPRIO: return pemu_fence_prio(0, a0, a1, 0, res);
    case PEMU_F_IOPRIO:
        if ((int) a0 == 1) { if ((int) a1 > 0 && !pemu_pid_owned((pid_t) (int) a1)) { *res = -ESRCH; return 1; } return 0; }
        if ((int) a0 == 2 || (int) a0 == 3) { *res = -EPERM; return 1; }
        return 0;
    case PEMU_F_CAPGET: {
        uint32_t h[2];
        if (!a0 || pemu_guest_copy(0, (void*) a0, h, sizeof h) < 0) return 0;      /* the kernel reports EFAULT */
        if ((int) h[1] > 0 && !pemu_pid_owned((pid_t) (int) h[1])) { *res = -ESRCH; return 1; }
        return 0; }
    }
    return 0;
}

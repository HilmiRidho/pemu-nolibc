/* src/proot/pemulog.inc.h - the negative-event log: ~/.pemu/pemu.log. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0
 *
 * pemu writes down everything that goes wrong or is not (fully) supported, so that it can be read afterwards without -strace:
 *   unsupported-syscall   a guest syscall pemu has no implementation for (answered ENOSYS)
 *   enosys                a syscall that was answered ENOSYS (declined on purpose: rseq, seccomp, io_uring ...; or the host said so)
 *   refused               a call pemu refuses on purpose (EPERM / ESRCH / EINVAL): ptrace, namespaces, pivot_root, PID fence, seccomp ...
 *   host-seccomp          the HOST's seccomp filter (Android, containers) blocked a syscall that pemu forwarded
 *   unsupported-insn      an instruction the interpreter does not implement (the guest gets SIGILL)
 *   jit-fallback          an instruction the JIT does not translate and hands to the interpreter (slower, but correct)
 *   guest-fault           a fatal fault or signal in the guest that no guest handler caught
 *   error                 a startup / load / exec / rootfs error, and pemu's own warnings
 * It is always on: there is no switch, option or environment variable for it, and it never writes to the guest's stdout / stderr.
 *
 * File: <HOME>/.pemu/pemu.log (next to <HOME>/.pemu/rootfs); without HOME: $PREFIX/var/lib/pemu/pemu.log (Termux).  Appended by
 * every pemu process of the tree, one write() per line.  Over 8 MiB the file is renamed to pemu.log.1 (one generation is kept).
 * Line:  2026-11-16T01:02:03Z pid=1234 [prog] kind: message
 * A repeated event is written once; at exit the process adds one line "repeated xN" (N = how many times in that process) for every event that happened more than once. */
#include <time.h>
#define PLOG_MAX_BYTES (8u << 20)
#define PLOG_SLOTS 1024                                  /* distinct events remembered per process (power of two) */
#define PLOG_LINE 640
static char plog_path[PATH_MAX];                         /* "" = no place to write; fixed at start (a guest can change HOME, not this) */
static char plog_prog[96];                               /* name of the guest program, for the line prefix */
static const char *const *plog_names; static unsigned plog_nnames;   /* guest syscall names (set by the hook: sysnames.inc.h) */
static __thread unsigned long plog_nr = ~0UL;            /* guest syscall this thread is inside, ~0 = none */
static __thread int plog_busy;                           /* no recursion: a signal handler may log while this thread is logging */
static __thread int plog_noted;                          /* the syscall in flight has been logged already (the central ENOSYS check skips it) */
struct plog_ent { volatile unsigned long long key; volatile unsigned cnt, own; char kind[24]; char msg[200]; };
static struct plog_ent plog_t[PLOG_SLOTS];
static volatile int plog_full;

/* once, first thing in main(): decide where the log goes, before anything can change the environment. No I/O here. */
static void plog_init(void) {
    if (plog_path[0]) return;
    const char *h = getenv("HOME"), *pf = getenv("PREFIX");
    if (h && *h) snprintf(plog_path, sizeof plog_path, "%s/.pemu/pemu.log", h);
    else if (pf && *pf) snprintf(plog_path, sizeof plog_path, "%s/var/lib/pemu/pemu.log", pf);
}
static void plog_set_names(const char *const *n, unsigned cnt) { plog_names = n; plog_nnames = cnt; }
static void plog_set_prog(const char *p) {
    if (!p) return;
    const char *b = strrchr(p, '/'); b = b && b[1] ? b + 1 : p;
    snprintf(plog_prog, sizeof plog_prog, "%.95s", b);
}
/* a re-exec'd pemu gets the path of the first one as an option (see pemu_reexec_opts): a guest may have changed its HOME */
static int plog_opt(const char *v) { if (v && *v) snprintf(plog_path, sizeof plog_path, "%s", v); return 0; }
static int plog_reexec_opts(char **nv, int k) {
    if (!plog_path[0]) return k;
    nv[k++] = "--log-file"; nv[k++] = strdup(plog_path);
    return k;
}
static const char *plog_sysname(unsigned long nr) {
    return (plog_names && nr < plog_nnames && plog_names[nr]) ? plog_names[nr] : "?";
}
static unsigned long long plog_hash(const char *s, unsigned long long h) {
    for (; *s; s++) { h ^= (unsigned char) *s; h *= 1099511628211ULL; }
    return h;
}
static void plog_mkdirs(char *p) {                       /* the directory of the log file, one level (.pemu or var/lib/pemu's last part) */
    char *sl = strrchr(p, '/'); if (!sl) return;
    *sl = 0; if (host_mkdir(p, 0755) < 0 && errno == ENOENT) {   /* $PREFIX/var/lib/pemu: the parents too */
        for (char *q = p + 1; *q; q++) if (*q == '/') { *q = 0; host_mkdir(p, 0755); *q = '/'; }
        host_mkdir(p, 0755);
    }
    *sl = '/';
}
static void plog_write(const char *line, size_t n) {
    if (!plog_path[0]) return;
    int saved = errno;
    int fd = host_open(plog_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);     /* opened per line: a guest that closes every fd cannot hide it */
    if (fd < 0 && errno == ENOENT) { char d[PATH_MAX]; snprintf(d, sizeof d, "%s", plog_path); plog_mkdirs(d); fd = host_open(plog_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600); }
    if (fd < 0) { errno = saved; return; }
    struct stat st, st2;
    if (host_fstat(fd, &st) == 0 && st.st_size > (off_t) PLOG_MAX_BYTES && host_stat(plog_path, &st2) == 0 && st2.st_ino == st.st_ino) {   /* rotate (a racing process may have done it already: the inode check) */
        char old[PATH_MAX + 4]; snprintf(old, sizeof old, "%s.1", plog_path);
        if (host_rename(plog_path, old) == 0) { host_close(fd); fd = host_open(plog_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600); if (fd < 0) { errno = saved; return; } }
    }
    if (host_write(fd, line, n) < 0) { }
    host_close(fd);
    errno = saved;
}
static size_t plog_prefix(char *b, size_t sz) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm; time_t t = ts.tv_sec; gmtime_r(&t, &tm);                          /* UTC: localtime_r takes locks, a signal handler logs too */
    int n = snprintf(b, sz, "%04d-%02d-%02dT%02d:%02d:%02dZ pid=%d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int) host_getpid());
    if (plog_prog[0] && n > 0 && (size_t) n < sz) n += snprintf(b + n, sz - (size_t) n, " [%s]", plog_prog);
    return n > 0 ? (size_t) n : 0;
}
/* Record one event.  key: what makes two events "the same" (0 = the whole message); the first one is written at once. */
static void plog_ev(const char *kind, unsigned long long key, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void plog_ev(const char *kind, unsigned long long key, const char *fmt, ...) {
    if (plog_busy) return;
    plog_busy = 1;
    char msg[200]; va_list ap; va_start(ap, fmt); int m = vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (m < 0) msg[0] = 0;
    unsigned long long k = plog_hash(kind, 14695981039346656037ULL);
    k = key ? (k ^ (key * 0x9E3779B97F4A7C15ULL)) : plog_hash(msg, k);
    k |= 1;                                                                          /* 0 marks a free slot */
    struct plog_ent *e = NULL; int fresh = 0;
    unsigned i = (unsigned) (k >> 7) & (PLOG_SLOTS - 1);
    for (unsigned n = 0; n < PLOG_SLOTS; n++, i = (i + 1) & (PLOG_SLOTS - 1)) {
        unsigned long long cur = __atomic_load_n(&plog_t[i].key, __ATOMIC_ACQUIRE);
        if (cur == 0) { unsigned long long ex = 0; if (__atomic_compare_exchange_n(&plog_t[i].key, &ex, k, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { e = &plog_t[i]; fresh = 1; break; } cur = ex; }
        if (cur == k) { e = &plog_t[i]; break; }
    }
    if (e && !fresh) {                                                               /* seen before (in this process, or in the parent that forked it) */
        __atomic_add_fetch(&e->cnt, 1, __ATOMIC_RELAXED);
        plog_busy = 0; return;
    }
    if (e) {
        snprintf(e->kind, sizeof e->kind, "%s", kind); snprintf(e->msg, sizeof e->msg, "%s", msg);
        e->own = 1; e->cnt = 1;
    } else if (__atomic_exchange_n(&plog_full, 1, __ATOMIC_RELAXED)) { plog_busy = 0; return; }       /* table full: one notice, then silence */
    else { snprintf(msg, sizeof msg, "too many distinct events, further new ones are not written"); kind = "error"; }
    char b[PLOG_LINE]; size_t n = plog_prefix(b, sizeof b);
    n += (size_t) snprintf(b + n, sizeof b - n, " %s: %s\n", kind, msg);
    if (n >= sizeof b) { n = sizeof b - 1; b[n - 1] = '\n'; }
    plog_write(b, n);
    plog_busy = 0;
}
/* at exit: one line for every event this process saw more than once */
static void plog_summary(void) {
    if (plog_busy) return;
    plog_busy = 1;
    for (unsigned i = 0; i < PLOG_SLOTS; i++) {
        struct plog_ent *e = &plog_t[i];
        unsigned c = e->cnt, o = e->own;
        if (!e->key || c <= o) continue;
        e->cnt = e->own = 0;                                                         /* once only */
        char b[PLOG_LINE]; size_t n = plog_prefix(b, sizeof b);
        n += (size_t) snprintf(b + n, sizeof b - n, " %s: repeated x%u: %s\n", e->kind, c, e->msg);
        if (n >= sizeof b) { n = sizeof b - 1; b[n - 1] = '\n'; }
        plog_write(b, n);
    }
    plog_busy = 0;
}
/* a fork child counts its own repeats only; what the parent already wrote is not written again */
static void plog_atfork_child(void) {
    plog_busy = 0;
    for (unsigned i = 0; i < PLOG_SLOTS; i++) { plog_t[i].cnt = 0; plog_t[i].own = 0; }
}

/* ---- helpers used by the hooks */
static void plog_unsupported(unsigned long nr, unsigned long long a0, unsigned long long a1) {
    plog_noted = 1;
    plog_ev("unsupported-syscall", nr + 1, "syscall %lu (%s) has no implementation, answered ENOSYS (a0=%#llx a1=%#llx)", nr, plog_sysname(nr), a0, a1);
}
/* the syscall just handled answered ENOSYS and nothing logged it yet */
static void plog_check_result(unsigned long nr, long r) {
    if (r == -ENOSYS && !plog_noted) plog_ev("enosys", nr + 1, "syscall %lu (%s) answered ENOSYS", nr, plog_sysname(nr));
    plog_noted = 0;
}
/* the syscall in flight is answered ENOSYS on purpose, with the reason (otherwise plog_check_result logs it without one) */
static void plog_enosys(const char *why) {
    unsigned long nr = plog_nr;
    plog_noted = 1;
    plog_ev("enosys", plog_hash(why, nr + 1), "syscall %lu (%s): %s", nr, nr == ~0UL ? "?" : plog_sysname(nr), why);
}
#define PLOG_ENOSYS(why) (plog_enosys(why), -ENOSYS)
/* a deliberate refusal inside the current syscall: tag is a short constant reason (also the dedupe key together with the syscall) */
static void plog_refuse(const char *why) {
    unsigned long nr = plog_nr;
    plog_noted = 1;
    plog_ev("refused", plog_hash(why, nr + 1), "syscall %lu (%s): %s", nr, nr == ~0UL ? "?" : plog_sysname(nr), why);
}
#define PLOG_REFUSE(err, why) (plog_refuse(why), -(err))
/* startup and runtime errors, "pemu: ..." messages */
static void plog_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void plog_error(const char *fmt, ...) {
    char m[200]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    size_t l = strlen(m); while (l && (m[l - 1] == '\n' || m[l - 1] == ' ')) m[--l] = 0;
    plog_ev("error", 0, "%s", !strncmp(m, "pemu: ", 6) ? m + 6 : m);
}
/* a message for the user on stderr AND a line in the log */
static void pemu_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void pemu_err(const char *fmt, ...) {
    char m[512]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    fputs(m, stderr);
    plog_error("%s", m);
}
/* the callback the CPU core reaches through PemuOS.negative */
static void plog_core_cb(const char *kind, unsigned long long key, const char *msg) { plog_ev(kind, key, "%s", msg); }

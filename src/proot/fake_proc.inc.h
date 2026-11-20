/* src/proot/fake_proc.inc.h - fake /proc files for hosts that hide them. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- fake /proc files: Android hides some of them (/proc/stat, loadavg, vmstat, ...). They are only
 * served when the real file cannot be opened (or always, with PEMU_FAKE_PROC=1). */
#include <sys/times.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
/* ---- /proc/mounts, /proc/<pid>/mounts, /proc/<pid>/mountinfo: the guest's view of the mount table.
 * /proc is a raw bind, so the host kernel would show the HOST's table: every host mount point, none of which exists in the rootfs
 * (`df` then fails with ENOENT on each and exits 1) and none of them is how the guest sees its own tree.  pemu has no mount
 * namespace, so the table is built here from three things: the rootfs as "/", host mounts that lie below the rootfs directory
 * (shown at their guest path), and pemu's own bind table (-b, mount -o bind, /dev /proc /sys).  The file system type and options of
 * a line come from the host mount that holds the directory behind it.  Always served (not only as a fallback), see pemu_open. */
#define PEMU_FP 16384                                    /* size of the buffer a fake /proc file is built in */
struct pemu_mi { const char *id, *dev, *root, *mp, *opts, *fst, *src, *sup; char mpu[PATH_MAX]; };
static int pemu_mi_load(char **buf, struct pemu_mi **out) {                  /* host mountinfo -> array; *buf and *out are heap, caller frees */
    *buf = NULL; *out = NULL;
    FILE *f = fopen("/proc/self/mountinfo", "re"); if (!f) return 0;
    size_t cap = 1 << 16, n = 0; char *b = malloc(cap); if (!b) { fclose(f); return 0; }
    for (;;) {
        if (n + 4096 + 1 > cap) { if (cap >= (1u << 22)) break; char *nb = realloc(b, cap * 2); if (!nb) break; b = nb; cap *= 2; }
        size_t r = fread(b + n, 1, cap - n - 1, f); if (r == 0) break; n += r;
    }
    fclose(f); b[n] = 0;
    int nl = 0; for (size_t i = 0; i < n; i++) if (b[i] == '\n') nl++;
    struct pemu_mi *a = calloc((size_t) nl + 1, sizeof *a); if (!a) { free(b); return 0; }
    int k = 0;
    for (char *ln = b; *ln && k < nl;) {
        char *e = strchr(ln, '\n'); if (!e) break; *e = 0;
        char *p = ln; int ok = 1;                                              /* id parent maj:min root mountpoint opts */
        char *tok[6];
        for (int i = 0; i < 6; i++) { while (*p == ' ') p++; if (!*p) { ok = 0; break; } tok[i] = p; while (*p && *p != ' ') p++; if (*p) *p++ = 0; }
        if (ok) {                                                            /* optional fields up to a lone "-" */
            char *q = p; int found = 0;
            while (*q) {
                while (*q == ' ') q++;
                char *s = q; while (*q && *q != ' ') q++;
                int last = !*q; if (!last) *q++ = 0;
                if (!strcmp(s, "-")) { found = 1; break; }
                if (last) break;
            }
            char *r[3] = { NULL, NULL, NULL };
            if (found) for (int i = 0; i < 3; i++) { while (*q == ' ') q++; if (!*q) break; r[i] = q; while (*q && *q != ' ') q++; if (*q) *q++ = 0; }
            if (found && r[0] && r[1]) {
                struct pemu_mi *m = &a[k++];
                m->id = tok[0]; m->dev = tok[2]; m->root = tok[3]; m->mp = tok[4]; m->opts = tok[5]; m->fst = r[0]; m->src = r[1]; m->sup = r[2] ? r[2] : "rw";
                size_t o = 0;                                                /* unescape \\ooo of the mount point, for comparisons */
                for (const char *s = tok[4]; *s && o + 1 < PATH_MAX; ) {
                    if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] >= '0' && s[2] <= '7' && s[3] >= '0' && s[3] <= '7') { m->mpu[o++] = (char) (((s[1] - '0') << 6) | ((s[2] - '0') << 3) | (s[3] - '0')); s += 4; }
                    else m->mpu[o++] = *s++;
                }
                m->mpu[o] = 0;
            }
        }
        ln = e + 1;
    }
    *buf = b; *out = a; return k;
}
/* the host mount that holds `path` (longest mount point; the later line wins a tie); -1 when the table is empty */
static int pemu_mi_of(const struct pemu_mi *a, int n, const char *path) {
    int best = -1; size_t bl = 0, pl = strlen(path);
    for (int i = 0; i < n; i++) {
        size_t l = strlen(a[i].mpu);
        if (l == 1 || (pl >= l && !memcmp(path, a[i].mpu, l) && (path[l] == '/' || !path[l]))) if (best < 0 || l >= bl) { best = i; bl = l; }
    }
    return best;
}
static void pemu_mnt_esc(const char *s, char *o, size_t sz) {          /* space, tab, newline, backslash as \\ooo, like the kernel */
    size_t k = 0;
    for (; *s && k + 5 < sz; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\\') k += (size_t) snprintf(o + k, sz - k, "\\%03o", (unsigned char) *s);
        else o[k++] = *s;
    }
    o[k] = 0;
}
/* one entry of the bind table: 0 = live (g, h, *ord filled), 1 = removed slot, -1 = past the end */
static int pemu_bind_at(int i, char *g, char *h, unsigned long *ord) {
    int rc; unsigned s;
    do {
        s = pemu_rd_begin();
        if (i >= pemu_nb) rc = -1;
        else if (pemu_b[i].off || pemu_b[i].gl >= PATH_MAX || pemu_b[i].hl >= PATH_MAX) rc = 1;
        else { memcpy(g, pemu_b[i].g, pemu_b[i].gl + 1); memcpy(h, pemu_b[i].h, pemu_b[i].hl + 1); *ord = pemu_b[i].ord; rc = 0; }
    } while (pemu_rd_retry(s));
    return rc;
}
/* one line of mounts (info = 0) or mountinfo (info = 1). hp = the host directory behind the mount, gp = its guest path.
 * m = the host mount that holds hp (NULL: unknown).  Returns the length, 0 when it does not fit in sz. */
static size_t pemu_mnt_line(int info, int id, int parent, const struct pemu_mi *m, const char *hp, const char *gp, int isroot, char *o, size_t sz) {
    char eg[2 * PATH_MAX]; pemu_mnt_esc(gp, eg, sizeof eg);
    const char *fst = m ? m->fst : "none", *src = isroot ? "rootfs" : m ? m->src : "none", *sup = m ? m->sup : "rw";
    const char *opts = m ? m->opts : "rw";
    const char *root = "/";                       /* the host's subtree path is a host path: not shown (hp is only used to pick `m`) */
    (void) hp;
    int n;
    if (info) n = snprintf(o, sz, "%d %d %s %s %s %s - %s %s %s\n", id, parent, m ? m->dev : "0:0", root, eg, opts, fst, src, sup);
    else {                                                                                   /* rw/ro first, then flags, then the file system's own options */
        const char *so = !strncmp(sup, "rw", 2) || !strncmp(sup, "ro", 2) ? (sup[2] == ',' ? sup + 3 : sup + 2) : sup;
        n = snprintf(o, sz, "%s %s %s %s%s%s 0 0\n", src, eg, fst, opts, *so ? "," : "", so);
    }
    return n > 0 && (size_t) n < sz ? (size_t) n : 0;
}
static int pemu_fakemounts(int info, char *b, size_t sz) {
    char rt[PATH_MAX]; pemu_root_get(rt);
    char *buf; struct pemu_mi *a; int n = pemu_mi_load(&buf, &a);
    size_t o = 0; int id = 1, full = 0;
#define EMIT(...) do { if (!full) { size_t w = pemu_mnt_line(info, __VA_ARGS__, b + o, sz - o); if (w) o += w; else full = 1; } } while (0)
    EMIT(id, 0, n ? &a[pemu_mi_of(a, n, rt)] : NULL, rt, "/", 1); id++;
    size_t rl = strlen(rt);
    for (int i = 0; i < n; i++) {                              /* host mounts below the rootfs directory, at their guest path */
        const char *mp = a[i].mpu; size_t l = strlen(mp);
        if (rl == 1 ? l > 1 : (l > rl && !memcmp(mp, rt, rl) && mp[rl] == '/')) { EMIT(id, 1, &a[i], mp, mp + (rl == 1 ? 0 : rl), 0); id++; }
    }
    int idx[PEMU_MAXB]; unsigned long ord[PEMU_MAXB]; int nb = 0;                 /* pemu's own mounts, oldest first */
    static __thread char g[PATH_MAX], h[PATH_MAX];
    for (int i = 1; i < PEMU_MAXB; i++) {
        unsigned long od; int r = pemu_bind_at(i, g, h, &od); if (r < 0) break; if (r) continue;
        int j = nb++; while (j > 0 && ord[j - 1] > od) { ord[j] = ord[j - 1]; idx[j] = idx[j - 1]; j--; } ord[j] = od; idx[j] = i;
    }
    for (int k = 0; k < nb; k++) {
        unsigned long od; if (pemu_bind_at(idx[k], g, h, &od) != 0) continue;
        EMIT(id, 1, n ? (pemu_mi_of(a, n, h) < 0 ? NULL : &a[pemu_mi_of(a, n, h)]) : NULL, h, g, 0); id++;
    }
#undef EMIT
    free(a); free(buf);
    return (int) o;
}
/* "/proc/mounts", "/proc/<pid>/mounts", "/proc/<pid>/task/<tid>/mounts" and the same for mountinfo. 1 = yes (*info says which) */
static int pemu_is_mounts(const char *g, int *info) {
    if (strncmp(g, "/proc/", 6)) return 0;
    const char *p = g + 6; 
    if (!strcmp(p, "mounts")) { if (info) *info = 0; return 1; }
    for (int pass = 0; pass < 2; pass++) {                      /* <pid>/ and then optionally task/<tid>/ */
        if (pass == 1) { if (strncmp(p, "task/", 5)) break; p += 5; }
        const char *d = p; while (*d >= '0' && *d <= '9') d++;
        if (d == p || *d != '/') return 0;
        p = d + 1;
        if (!strcmp(p, "mounts") || !strcmp(p, "mountinfo")) { if (info) *info = p[5] == 'i'; return 1; }
    }
    return 0;
}
static int pemu_fakeproc(const char *g, char *b, size_t sz) {
    { int mi; if (pemu_is_mounts(g, &mi)) return pemu_fakemounts(mi, b, sz); }
    struct timespec ts; double up = 0;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) up = (double) ts.tv_sec + ts.tv_nsec / 1e9;
    long ncpu = sysconf(_SC_NPROCESSORS_CONF); if (ncpu < 1) ncpu = 1;
    if (!strcmp(g, "/proc/uptime")) return snprintf(b, sz, "%.2f %.2f\n", up, up * (double) ncpu * 0.9);
    if (!strcmp(g, "/proc/loadavg")) { double l[3] = { 0, 0, 0 }; struct sysinfo si; if (sysinfo(&si) == 0) for (int i = 0; i < 3; i++) l[i] = (double) si.loads[i] / 65536.0; /* getloadavg() is bionic API 29+ only */ return snprintf(b, sz, "%.2f %.2f %.2f 1/1 %d\n", l[0], l[1], l[2], (int) host_getpid()); }
    if (!strcmp(g, "/proc/version")) { struct utsname u; if (uname(&u)) strcpy(u.release, "6.1.0"); return snprintf(b, sz, "Linux version %s (pemu@localhost) (gcc) #1 SMP PREEMPT\n", u.release); }
    if (!strcmp(g, "/proc/sys/kernel/cap_last_cap")) return snprintf(b, sz, "40\n");
    if (!strcmp(g, "/proc/vmstat")) return snprintf(b, sz, "nr_free_pages 0\nnr_inactive_anon 0\nnr_active_anon 0\nnr_inactive_file 0\nnr_active_file 0\npgpgin 0\npgpgout 0\npswpin 0\npswpout 0\npgfault 0\npgmajfault 0\n");
    if (!strcmp(g, "/proc/stat")) {
        struct tms t; clock_t k = times(&t); (void) k;
        long hz = sysconf(_SC_CLK_TCK); if (hz < 1) hz = 100;
        unsigned long u = (unsigned long) t.tms_utime + (unsigned long) t.tms_cutime, sy = (unsigned long) t.tms_stime + (unsigned long) t.tms_cstime;
        unsigned long tot = (unsigned long) (up * (double) hz), idle = tot > u + sy ? tot - u - sy : 0;
        size_t o = (size_t) snprintf(b, sz, "cpu  %lu 0 %lu %lu 0 0 0 0 0 0\n", u, sy, idle * (unsigned long) ncpu);
        for (long i = 0; i < ncpu && o < sz; i++) o += (size_t) snprintf(b + o, sz - o, "cpu%ld %lu 0 %lu %lu 0 0 0 0 0 0\n", i, u / (unsigned long) ncpu, sy / (unsigned long) ncpu, idle);
        if (o < sz) o += (size_t) snprintf(b + o, sz - o, "intr 0\nctxt 0\nbtime %ld\nprocesses 1\nprocs_running 1\nprocs_blocked 0\n", (long) time(NULL) - (long) up);
        return (int) (o < sz ? o : sz - 1);
    }
    return -1;
}
/* memfd with the fake content; LONG_MIN = `g` is not a fake file */
static long pemu_fakeproc_open(const char *g, long hf) {
    if (strncmp(g, "/proc/", 6)) return LONG_MIN;
    char b[PEMU_FP]; int n = pemu_fakeproc(g, b, sizeof b);
    if (n < 0) return LONG_MIN;
    if (hf & (O_WRONLY | O_RDWR)) return -EACCES;
    long fd = hs(HOP_memfd_create, (long) "pemu-proc", (hf & O_CLOEXEC) ? 1 : 0, 0, 0, 0, 0);   /* 1 = MFD_CLOEXEC */
    if (fd < 0) return fd;
    if (host_write((int) fd, b, (size_t) n) != n || host_lseek((int) fd, 0, SEEK_SET) < 0) { host_close((int) fd); return -EIO; }
    return fd;
}

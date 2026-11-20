/* src/proot/bind.inc.h - path normalisation and the bind table. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* lexical normalisation of an absolute path, in place */
static void pemu_norm(char *p) {
    static __thread char out[PATH_MAX]; size_t o = 0; out[0] = 0;
    for (char *s = p; *s;) {
        while (*s == '/') s++;
        if (!*s) break;
        char *e = s; while (*e && *e != '/') e++;
        size_t n = (size_t)(e - s);
        if (n == 1 && s[0] == '.') { }
        else if (n == 2 && s[0] == '.' && s[1] == '.') { while (o > 0 && out[o - 1] != '/') o--; if (o) o--; out[o] = 0; }
        else { if (o + n + 2 >= PATH_MAX) break; out[o++] = '/'; memcpy(out + o, s, n); o += n; out[o] = 0; }
        s = e;
    }
    if (!o) { out[0] = '/'; out[1] = 0; }
    strcpy(p, out);
}

/* ---- seqlock over the table.  Readers (pemu_mnt_of, pemu_g2h, pemu_h2g, pemu_root_get) copy what they need and retry when an edit
 * ran meanwhile; so a thread never sees a half-moved g/gl pair, a half-rewritten slot or a half-written pemu_root. */
static unsigned pemu_rd_begin(void) {
    unsigned s;
    while (((s = __atomic_load_n(&pemu_seq, __ATOMIC_ACQUIRE)) & 1u)) sched_yield();
    return s;
}
static int pemu_rd_retry(unsigned s) { __atomic_thread_fence(__ATOMIC_ACQUIRE); return __atomic_load_n(&pemu_seq, __ATOMIC_RELAXED) != s; }
/* writers: the caller holds pemu_lock() (or runs single-threaded during setup) */
static void pemu_tab_begin(void) { __atomic_store_n(&pemu_seq, pemu_seq + 1, __ATOMIC_RELAXED); __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void pemu_tab_end(void)   { __atomic_store_n(&pemu_seq, pemu_seq + 1, __ATOMIC_RELEASE); }

/* first free slot (never 0, the rootfs), else the end of the table, else -1 */
static int pemu_slot(void) {
    for (int i = 1; i < pemu_nb; i++) if (pemu_b[i].off) return i;
    return pemu_nb < PEMU_MAXB ? pemu_nb : -1;
}
/* Add a bind.  Returns 0, or -errno: -ENOSPC table full, -ENOENT/-... host path unusable, -EINVAL guest path not absolute.
 * A slot freed by umount (or by chroot) is reused, so mount/umount cycles never exhaust the table. */
static int pemu_add_bind(const char *h, const char *g) {
    char rh[PATH_MAX], gg[PATH_MAX];
    if (g[0] != '/') return -EINVAL;
    if (!realpath(h, rh)) return -(errno ? errno : ENOENT);
    int slot = pemu_slot(); if (slot < 0) return -ENOSPC;
    snprintf(gg, sizeof gg, "%s", g); pemu_norm(gg);
    struct pemu_bind *b = &pemu_b[slot];
    pemu_tab_begin();
    memcpy(b->g, gg, strlen(gg) + 1); memcpy(b->h, rh, strlen(rh) + 1);
    b->gl = strlen(gg); b->hl = strlen(rh);
    b->raw = (!strcmp(rh, "/proc") || !strcmp(rh, "/sys") || !strcmp(rh, "/dev")) ? 1 : 0;
    b->ord = ++pemu_ord; b->off = 0;
    if (slot == pemu_nb) pemu_nb++;
    pemu_tab_end();
    return 0;
}

/* The mount that serves a canonical guest path: the longest matching bind, the most recently added one on a tie (index 0 = the
 * rootfs).  *gl / *raw (optional) are that entry's guest-path length and raw flag, read in the same consistent snapshot. */
static int pemu_pick(const char *g, size_t *gl, int *raw) {
    size_t n = strlen(g); int best; size_t bl; int rw; unsigned s;
    do {
        s = pemu_rd_begin(); best = 0; bl = 0; rw = 0; unsigned long bo = 0;
        for (int i = 0; i < pemu_nb; i++) {
            const struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
            size_t l = b->gl;
            if (l == 1 || (l < PATH_MAX && n >= l && !memcmp(g, b->g, l) && (g[l] == '/' || !g[l])))
                if (l > bl || (l == bl && b->ord >= bo)) { best = i; bl = l; bo = b->ord; rw = b->raw; }
        }
    } while (pemu_rd_retry(s));
    if (gl) *gl = bl;
    if (raw) *raw = rw;
    return best;
}
/* index of the bind (mount) that serves a canonical guest path */
static int pemu_mnt_of(const char *g) { return pemu_pick(g, NULL, NULL); }
/* 1 when the rootfs entry itself (not a bind, not a raw /proc /sys /dev) serves the path */
static int pemu_in_rootfs(const char *g) { size_t gl; int raw; pemu_pick(g, &gl, &raw); return gl == 1 && !raw; }

/* guest path (canonical) -> host path. The longest matching bind wins; later binds win ties. */
static int pemu_g2h(const char *g, char *out, int *raw) {
    size_t n = strlen(g); int rc, rw; unsigned s;
    do {
        s = pemu_rd_begin(); int best = 0; size_t bl = 0; unsigned long bo = 0;
        for (int i = 0; i < pemu_nb; i++) {
            const struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
            size_t l = b->gl;
            if (l == 1 || (l < PATH_MAX && n >= l && !memcmp(g, b->g, l) && (g[l] == '/' || !g[l])))
                if (l > bl || (l == bl && b->ord >= bo)) { best = i; bl = l; bo = b->ord; }
        }
        const struct pemu_bind *b = &pemu_b[best];
        size_t hl = b->hl; rw = b->raw;
        const char *suf = g + (bl == 1 ? 0 : bl);
        size_t sl = strlen(suf);
        if (hl >= PATH_MAX) rc = -ENAMETOOLONG;                      /* torn read: retried below */
        else if (hl == 1) { const char *q = sl ? suf : "/"; if (!sl) sl = 1; if (sl >= PEMU_HP) sl = PEMU_HP - 1; memcpy(out, q, sl); out[sl] = 0; rc = 0; }
        else if (sl + hl >= PEMU_HP) rc = -ENAMETOOLONG;
        else { memcpy(out, b->h, hl); memcpy(out + hl, suf, sl + 1); rc = 0; }   /* hl == strlen(b->h): pemu_add_bind sets both; a torn pair is retried */
    } while (pemu_rd_retry(s));
    if (rc == 0 && raw) *raw = rw;
    return rc;
}
/* host path -> guest path (for /proc/self/fd/N and the like); -1 when it is outside every bind */
static int pemu_h2g(const char *h, char *out) {
    size_t n = strlen(h); int rc; unsigned s;
    do {
        s = pemu_rd_begin(); int best = -1; size_t bl = 0;
        for (int i = 0; i < pemu_nb; i++) {
            const struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
            size_t l = b->hl;
            if (l == 1 || (l < PATH_MAX && n >= l && !memcmp(h, b->h, l) && (h[l] == '/' || !h[l])))
                if (best < 0 || l > bl) { best = i; bl = l; }
        }
        if (best < 0) rc = -1;
        else {
            const struct pemu_bind *b = &pemu_b[best];
            const char *suf = h + (bl == 1 ? 0 : bl);
            snprintf(out, PATH_MAX, "%s%s", b->gl == 1 ? "" : b->g, suf);
            rc = 0;
        }
    } while (pemu_rd_retry(s));
    if (rc == 0 && !*out) strcpy(out, "/");
    return rc;
}
/* the rootfs directory on the host, copied consistently (chroot rewrites it) */
static void pemu_root_get(char *out) {
    unsigned s;
    do { s = pemu_rd_begin(); memcpy(out, pemu_root, PATH_MAX); out[PATH_MAX - 1] = 0; } while (pemu_rd_retry(s));
}
static int pemu_fd2g_slow(int fd, char *g) {
    char l[64], h[PATH_MAX];
    snprintf(l, sizeof l, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(l, h, sizeof h - 1);
    if (n <= 0) return -1;
    h[n] = 0;
    return h[0] == '/' ? pemu_h2g(h, g) : -1;
}
/* dirfd -> guest path.  Every *at() call with a real dirfd needs it (find, rm -rf, tar, ls -R, dpkg, fts...), and the readlink of
 * /proc/self/fd/N cost about three times the call it served.  A small cache, keyed by fd number, is checked on EVERY use:
 *   - fstat(fd): same device and inode, still a directory, still linked (a closed and reused fd number, or a removed directory, misses);
 *   - the bind table seqlock (pemu_seq): a mount / umount / chroot changes what a host path means;
 *   - pemu_fdc_gen: this process renamed or removed a directory, which an open fd follows.
 * Not noticed: a directory renamed by ANOTHER process while this one holds a dirfd into it.  Build with -DPEMU_NO_FDCACHE to switch it off. */
#ifndef PEMU_NO_FDCACHE
#define PEMU_FDC_N 64
#define PEMU_FDC_L 256                                     /* longer guest paths are not cached */
static struct { int fd; uint64_t dev, ino; unsigned seq, gen; char g[PEMU_FDC_L]; } pemu_fdc[PEMU_FDC_N];
static volatile unsigned pemu_fdc_gen;
static void pemu_fdc_bump(void) { __atomic_add_fetch(&pemu_fdc_gen, 1, __ATOMIC_SEQ_CST); }     /* call AFTER the rename / rmdir has been done */
static int pemu_fd2g(int fd, char *g) {
    struct stat st;
    if (fd < 0 || host_fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_nlink == 0) return pemu_fd2g_slow(fd, g);
    unsigned seq = __atomic_load_n(&pemu_seq, __ATOMIC_ACQUIRE), gen = __atomic_load_n(&pemu_fdc_gen, __ATOMIC_SEQ_CST);
    if (seq & 1u) return pemu_fd2g_slow(fd, g);            /* a table edit is running */
    int ix = fd & (PEMU_FDC_N - 1), hit = 0;
    while (__sync_lock_test_and_set(&pemu_fdclk, 1)) sched_yield();
    if (pemu_fdc[ix].fd == fd && pemu_fdc[ix].dev == (uint64_t) st.st_dev && pemu_fdc[ix].ino == (uint64_t) st.st_ino &&
        pemu_fdc[ix].seq == seq && pemu_fdc[ix].gen == gen) { strcpy(g, pemu_fdc[ix].g); hit = 1; }
    __sync_lock_release(&pemu_fdclk);
    if (hit) return 0;
    int rc = pemu_fd2g_slow(fd, g);
    if (rc == 0 && strlen(g) < PEMU_FDC_L) {               /* store only if nothing moved while the slow path ran (and the fd is still the same directory) */
        struct stat s2;
        if (host_fstat(fd, &s2) == 0 && s2.st_dev == st.st_dev && s2.st_ino == st.st_ino &&
            __atomic_load_n(&pemu_seq, __ATOMIC_ACQUIRE) == seq && __atomic_load_n(&pemu_fdc_gen, __ATOMIC_SEQ_CST) == gen) {
            while (__sync_lock_test_and_set(&pemu_fdclk, 1)) sched_yield();
            pemu_fdc[ix].fd = fd; pemu_fdc[ix].dev = (uint64_t) st.st_dev; pemu_fdc[ix].ino = (uint64_t) st.st_ino;
            pemu_fdc[ix].seq = seq; pemu_fdc[ix].gen = gen; strcpy(pemu_fdc[ix].g, g);
            __sync_lock_release(&pemu_fdclk);
        }
    }
    return rc;
}
#else
static void pemu_fdc_bump(void) { }
static int pemu_fd2g(int fd, char *g) { return pemu_fd2g_slow(fd, g); }
#endif

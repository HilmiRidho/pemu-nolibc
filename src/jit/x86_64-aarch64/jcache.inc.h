/* src/jit/x86_64-aarch64/jcache.inc.h - persistent translation cache (PEMU_JIT_CACHE=<dir>). SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ persistent translation cache
 *
 * Off unless PEMU_JIT_CACHE names an absolute directory (read by the FIRST pemu only; a re-exec'd pemu gets the decision as the hidden --jit-cache, so a guest
 * cannot point it somewhere else through its environment).
 *
 * What is stored: the machine code of a translated block, as jtranslate emitted it, plus what is needed to put it in another run's code cache:
 *   - the guest pc it starts at, the translator options it was made with (g_jopt, g_jmt, g_jacq, g_jrcpc: all of them change the code),
 *   - a hash of every guest page the translator read (the pages of the block's instructions, <= 4). The code embeds guest addresses and was made from guest
 *     bytes, so it is valid exactly when the same bytes sit at the same address: a library mapped elsewhere, or a different build of it, simply does not match
 *     and is translated again,
 *   - the words that depend on the host and how to fix them (JRel: a branch to the exit stub jepi, the address of a C helper kept as an offset from jit_anchor),
 *   - the host-pc -> guest-rip map of the block (what jit_pc2rip needs after a fault).
 * A block emitted for the cache has no direct branch into another block and no code-relative adrp (see tcg_target.inc.h); its exits chain when first used.
 * Anything else that is host dependent makes the block "bad" and it is not stored; PEMU_JIT_CACHE_VERIFY=1 translates every block that IS in the file again and
 * compares it word for word with the relocated copy, which is how a missing relocation shows up (tests/integration/jit_cache.sh runs the JIT test guests that way).
 *
 * The file is <dir>/jit-<hash of the emulator binary>.pjc: a build of pemu never reads blocks of another build. It is append-only: a process collects the blocks it
 * translated and writes them in one go (flock) when it exits, when it execs, and whenever 1 MiB is pending. Records carry their own length and checksum, so a
 * torn or damaged record is skipped (the reader resynchronises on the next valid one); the checksum is checked when a record is used, not when the file is opened.
 * Not a security feature: the checksum is no signature. The directory and the file must belong to the user running pemu and must not be accessible to others, or they
 * are ignored; code from the file runs in the emulator process, outside the guest, so keep the directory out of any path the guest can reach.
 * Do not truncate the file while a pemu runs (it is mapped; deleting it is fine). The file stops growing at JC_MAXFILE. */
#include <sys/file.h>
#include <time.h>

#define JC_HDR       64u
#define JC_FILEMAGIC 0x31434a4d45504a50ULL            /* "PJPEMJC1" */
#define JC_MAGIC     0x43454a50u
#define JC_MAXFILE   (128ull << 20)
#define JC_WBUF_MAX  (1u << 20)
#define JC_NPG       4

typedef struct { u32 magic, len; u64 sum, pc; u32 opt, flags, nw, nrel, nmap, npg; u64 pg[JC_NPG], ph[JC_NPG]; } JCRec;   /* then: nw words (padded to 8 bytes), nrel JRel, nmap JCMap */
typedef struct { u32 w, pad; u64 ip; } JCMap;
_Static_assert(sizeof(JCRec) == 112 && sizeof(JRel) == 16 && sizeof(JCMap) == 16, "jcache record layout");

extern char __executable_start[] __attribute__((weak));
extern char etext[] __attribute__((weak));
static char g_jc_dir[1024], g_jc_path[1100];
static u64 g_jc_binid;
static const u8 *jc_map; static size_t jc_maplen; static u32 *jc_idx; static u32 jc_bits;
static u8 *jc_wbuf; static size_t jc_wlen, jc_wcap; static volatile int jc_wreq;      /* jmu protects the buffer */
static int g_jc_verify;
static __thread const JCRec *jc_vr;                          /* verify mode: the stored copy of the block being translated now */
static void jit_stw_locked(void);
static u64 jc_ns(void);
/* PEMU_JIT_CACHE_VERIFY also leaves a random gap (0-7 words, never executed) before every block, so that a block lands at a different offset in every run: a word that
 * should have been relocated but was not then differs, instead of coincidentally being right at the same offset. */
static u32 jc_jitter(void) { static u32 s; if (!s) s = (u32) host_getpid() * 2654435761u ^ (u32) jc_ns(); s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 7) & 7; }

static int jpers_helper_ok(void *f) { return __executable_start && etext && (char*) f >= __executable_start && (char*) f < etext; }
static inline u64 jc_rotl(u64 x, int r) { return x << r | x >> (64 - r); }
static u64 jc_hash(const void *p, size_t n) {                     /* not cryptographic: it guards against stale and damaged data. Four independent lanes: ~3.5x faster on a page */
    const u8 *b = p; u64 h = 0x243F6A8885A308D3ULL ^ (u64) n;
    if (n >= 32) {
        u64 a0 = h, a1 = h ^ 0x1111111111111111ULL, a2 = h ^ 0x2222222222222222ULL, a3 = h ^ 0x3333333333333333ULL;
        for (; n >= 32; n -= 32, b += 32) {
            u64 w0, w1, w2, w3; memcpy(&w0, b, 8); memcpy(&w1, b + 8, 8); memcpy(&w2, b + 16, 8); memcpy(&w3, b + 24, 8);
            a0 = (a0 ^ w0) * 0x9E3779B97F4A7C15ULL; a0 ^= a0 >> 32; a1 = (a1 ^ w1) * 0x9E3779B97F4A7C15ULL; a1 ^= a1 >> 32;
            a2 = (a2 ^ w2) * 0x9E3779B97F4A7C15ULL; a2 ^= a2 >> 32; a3 = (a3 ^ w3) * 0x9E3779B97F4A7C15ULL; a3 ^= a3 >> 32;
        }
        h = a0 ^ jc_rotl(a1, 17) ^ jc_rotl(a2, 31) ^ jc_rotl(a3, 47);
    }
    for (; n >= 8; n -= 8, b += 8) { u64 w; memcpy(&w, b, 8); h = (h ^ w) * 0x9E3779B97F4A7C15ULL; h ^= h >> 32; }
    if (n) { u64 w = 0; memcpy(&w, b, n); h = (h ^ w) * 0x9E3779B97F4A7C15ULL; h ^= h >> 32; }
    return h;
}
/* Hash of a guest code page, remembered per thread until the guest next maps, unmaps or re-protects anything (g_jc_epoch, bumped by jit_post_syscall): the
 * blocks of one page are looked up one after the other, and the JIT as a whole already assumes that a page that is not writable+executable does not change
 * without such a call. */
static volatile u32 g_jc_epoch;
static __thread struct { u64 pg, h; u32 ep; } jc_pm[256];
static u64 jc_page_hash(u64 pg) {
    u32 ep = __atomic_load_n(&g_jc_epoch, __ATOMIC_ACQUIRE) + 1;     /* +1: an all-zero slot never matches */
    unsigned k = (unsigned) ((pg ^ (pg >> 8)) & 255);
    if (jc_pm[k].pg == pg && jc_pm[k].ep == ep) return jc_pm[k].h;
    u64 h = jc_hash((const void*)(uintptr_t) (pg << 12), 4096);
    jc_pm[k].pg = pg; jc_pm[k].h = h; jc_pm[k].ep = ep;
    return h;
}
static u64 jc_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec; }
static inline u32 jc_flags(void) { return (u32) g_jmt | (u32) g_jacq << 1 | (u32) g_jrcpc << 3; }
static inline const u32 *jc_code(const JCRec *r) { return (const u32*) (r + 1); }
static inline const JRel *jc_rels(const JCRec *r) { return (const JRel*) ((const u8*) (r + 1) + (((size_t) r->nw * 4 + 7) & ~7ULL)); }
static inline const JCMap *jc_maps(const JCRec *r) { return (const JCMap*) (jc_rels(r) + r->nrel); }

/* the guest pages a block reads (the ones jpg_add registers) and their hashes; -1: more than JC_NPG pages, not stored */
static int jc_pages(const JI *ji, int n, u64 *pg, u64 *ph) {
    int np = 0;
    for (int i = 0; i < n; i++)
        for (int e = 0; e < 2; e++) {
            if (e && !ji[i].len) break;
            u64 p = (e ? ji[i].ip + ji[i].len - 1 : ji[i].ip) >> 12; int f = 0;
            for (int k = 0; k < np; k++) if (pg[k] == p) { f = 1; break; }
            if (!f) { if (np == JC_NPG) return -1; pg[np++] = p; }
        }
    for (int k = 0; k < np; k++) ph[k] = jc_page_hash(pg[k]);
    return np;
}

/* the block as it would sit at address `at`: the stored words with every host dependent one rewritten (written to dst, which may be `at` itself) */
static void jc_materialize(const JCRec *r, u32 *dst, const u32 *at) {
    memcpy(dst, jc_code(r), (size_t) r->nw * 4);
    const JRel *rl = jc_rels(r);
    for (u32 i = 0; i < r->nrel; i++) {
        u32 w = rl[i].w;
        if (rl[i].kind == JR_JEPI) dst[w] = 0x14000000u | ((u32) (jepi - (at + w)) & 0x3ffffff);
        else {
            u64 f = (u64) (uintptr_t) jit_anchor + rl[i].val;
            dst[w] = 0xD2800000u | ((u32) f & 0xffff) << 5 | 16u;
            for (int h = 1; h < 4; h++) dst[w + (u32) h] = 0xF2800000u | (u32) h << 21 | ((u32) (f >> (16 * h)) & 0xffff) << 5 | 16u;
        }
    }
}

/* ---- reading */
static size_t jc_rec_len(const u8 *m, size_t len, size_t off) {   /* length of the record whose header is at off, 0 if it does not look like one */
    if ((off & 7) || off + sizeof(JCRec) > len) return 0;
    const JCRec *r = (const JCRec*) (m + off);
    if (r->magic != JC_MAGIC || r->len < sizeof(JCRec) || (r->len & 7) || r->len > len - off) return 0;
    return r->len;
}
static int jc_sum_ok(const JCRec *r) { return jc_hash((const u8*) r + 16, r->len - 16) == r->sum; }
static u32 jc_walk(const u8 *m, size_t len, u32 *idx, u32 bits) {          /* counts the records; with idx also enters them; skips damage */
    u32 n = 0, mask = (1u << bits) - 1;
    size_t off = JC_HDR;
    while (off + sizeof(JCRec) <= len) {
        size_t l = jc_rec_len(m, len, off);
        if (!l) {                                                 /* damage (a torn write): look for the next record whose checksum holds */
            for (off += 8; off + sizeof(JCRec) <= len; off += 8) { l = jc_rec_len(m, len, off); if (l && jc_sum_ok((const JCRec*) (m + off))) break; l = 0; }
            if (!l) break;
        }
        if (idx) { const JCRec *r = (const JCRec*) (m + off); u32 h = hpc(r->pc, (int) bits); while (idx[h]) h = (h + 1) & mask; idx[h] = (u32) (off >> 3); }
        n++; off += l;
    }
    return n;
}
static void jc_load(void) {
    int fd = host_open(g_jc_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    if (host_fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) || st.st_size < (off_t) (JC_HDR + sizeof(JCRec))) { host_close(fd); return; }
    void *m = host_mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    host_close(fd);
    if (m == MAP_FAILED) return;
    const u64 *h = m;
    if (h[0] != JC_FILEMAGIC || h[1] != g_jc_binid) { host_munmap(m, (size_t) st.st_size); return; }
    u32 cnt = jc_walk(m, (size_t) st.st_size, NULL, 0), bits = 10;
    while ((1u << bits) < 2 * cnt + 2) bits++;
    u32 *idx = calloc((size_t) 1 << bits, sizeof *idx);
    if (!idx) { host_munmap(m, (size_t) st.st_size); return; }
    jc_walk(m, (size_t) st.st_size, idx, bits);
    jc_map = m; jc_maplen = (size_t) st.st_size; jc_bits = bits; jc_idx = idx;
}
/* a stored record that may be used now: well formed, its checksum holds and the guest still has the code it was made from (the guest reads can fault: the
 * caller holds no lock, as the decoder of jtranslate) */
static int jc_check(const JCRec *r) {
    if (!r->nw || r->nw > JBLOCK_MAX || r->nrel > JREL_MAX || !r->nmap || r->nmap > JMAXI || r->npg < 1 || r->npg > JC_NPG) return 0;
    if (r->len != sizeof(JCRec) + (((size_t) r->nw * 4 + 7) & ~7ULL) + (size_t) r->nrel * sizeof(JRel) + (size_t) r->nmap * sizeof(JCMap)) return 0;
    if (!jc_sum_ok(r)) return 0;
    const JRel *rl = jc_rels(r);
    for (u32 i = 0; i < r->nrel; i++) {
        if (rl[i].kind == JR_JEPI) { if (rl[i].w >= r->nw) return 0; }
        else if (rl[i].kind == JR_HELPER) { if ((u64) rl[i].w + 4 > r->nw) return 0; }
        else return 0;
    }
    const JCMap *mp = jc_maps(r);
    for (u32 i = 0; i < r->nmap; i++) if (mp[i].w >= r->nw || (i && mp[i].w < mp[i - 1].w)) return 0;
    for (u32 k = 0; k < r->npg; k++) if (jc_page_hash(r->pg[k]) != r->ph[k]) return 0;
    return 1;
}
static const JCRec *jc_find(u64 pc) {
    if (!jc_idx) return NULL;
    u32 mask = (1u << jc_bits) - 1, h = hpc(pc, (int) jc_bits), fl = jc_flags();
    for (; jc_idx[h]; h = (h + 1) & mask) {
        const JCRec *r = (const JCRec*) (jc_map + ((size_t) jc_idx[h] << 3));
        if (r->pc != pc || r->opt != g_jopt || r->flags != fl) continue;
        if (jc_check(r)) return r;
    }
    return NULL;
}
/* put a stored block into the code cache (jmu held) */
static void jc_install(const JCRec *r, u64 pc) {
    if ((u64) (jlim - jcur) < 2 * JBLOCK_MAX || jmapn + 4 * JMAXI > JMAP_CAP || jhn * 2 > (1u << JH_BITS) || jpgn > (1u << JPG_BITS) / 2) jit_stw_locked();
    if (g_jc_verify) jcur += jc_jitter();
    u32 *start = jcur;
    jc_materialize(r, start, start);
    const JCMap *mp = jc_maps(r); u32 base = (u32) ((u8*) start - (u8*) jbase);
    for (u32 i = 0; i < r->nmap; i++) {
        u32 k = jmapn; jm_off[k] = base + mp[i].w * 4; jm_pc[k] = mp[i].ip;
        __atomic_store_n(&jmapn, k + 1, __ATOMIC_RELEASE);
    }
    for (u32 k = 0; k < r->npg; k++) jpg_add(r->pg[k]);
    jcur = start + r->nw;
    j_sync_icache(start, jcur);
    jins(pc, start);
}
/* 1: the block is in the code cache now (loaded here or by another thread); 0: translate it */
static int jcache_try(u64 pc) {
    const JCRec *r = jc_find(pc);
    if (!r) { __atomic_fetch_add(&jst_cmiss, 1, __ATOMIC_RELAXED); return 0; }
    if (g_jc_verify) { jc_vr = r; return 0; }
    pthread_mutex_lock(&jmu);
    if (!jfind(pc)) { jc_install(r, pc); jst_cload++; }
    pthread_mutex_unlock(&jmu);
    return 1;
}

/* ---- writing (a block that was just emitted at start..jcur; jmu held; k0 = jmapn before its first instruction) */
static void jc_record(u64 pc, const u32 *start, u32 k0, const u64 *pg, const u64 *ph, int npg) {
    u32 nw = (u32) (jcur - start), nmap = jmapn - k0;
    if (!nw || nw > JBLOCK_MAX || !nmap || nmap > JMAXI) return;
    size_t len = sizeof(JCRec) + (((size_t) nw * 4 + 7) & ~7ULL) + (size_t) jnrel * sizeof(JRel) + (size_t) nmap * sizeof(JCMap);
    if (jc_wlen + len > jc_wcap) {
        size_t nc = jc_wcap ? jc_wcap * 2 : (256u << 10);
        while (nc < jc_wlen + len) nc *= 2;
        u8 *nb = realloc(jc_wbuf, nc);
        if (!nb) return;
        jc_wbuf = nb; jc_wcap = nc;
    }
    JCRec *r = (JCRec*) (jc_wbuf + jc_wlen);
    memset(r, 0, len);
    r->magic = JC_MAGIC; r->len = (u32) len; r->pc = pc; r->opt = g_jopt; r->flags = jc_flags(); r->nw = nw; r->nrel = jnrel; r->nmap = nmap; r->npg = (u32) npg;
    for (int k = 0; k < npg; k++) { r->pg[k] = pg[k]; r->ph[k] = ph[k]; }
    memcpy((u32*) jc_code(r), start, (size_t) nw * 4);
    memcpy((JRel*) jc_rels(r), jrel, (size_t) jnrel * sizeof(JRel));
    JCMap *mp = (JCMap*) jc_maps(r); u32 base = (u32) ((u8*) start - (u8*) jbase);
    for (u32 i = 0; i < nmap; i++) { mp[i].w = (jm_off[k0 + i] - base) / 4; mp[i].ip = jm_pc[k0 + i]; }
    r->sum = jc_hash((const u8*) r + 16, len - 16);
    jc_wlen += len; jst_crec++;
    if (jc_wlen >= JC_WBUF_MAX) jc_wreq = 1;
}
/* verify mode: the block just emitted must equal the stored one placed at the same address */
static void jc_verify(const JCRec *r, const u32 *start, u32 k0) {
    u32 nw = (u32) (jcur - start); const char *why = NULL; u32 at = 0;
    static u32 tmp[JBLOCK_MAX];                                   /* jmu held: one user at a time */
    if (jpers_bad) why = "the block is not storable now but is in the file";
    else if (nw != r->nw) why = "length differs";
    else if (jmapn - k0 != r->nmap) why = "map size differs";
    else {
        jc_materialize(r, tmp, start);
        for (u32 i = 0; i < nw; i++) if (tmp[i] != start[i]) { why = "word differs"; at = i; break; }
        const JCMap *mp = jc_maps(r); u32 base = (u32) ((u8*) start - (u8*) jbase);
        for (u32 i = 0; !why && i < r->nmap; i++) if (jm_off[k0 + i] != base + mp[i].w * 4 || jm_pc[k0 + i] != mp[i].ip) why = "map entry differs";
    }
    if (!why) { jst_cverok++; return; }
    jst_cbad++;
    fprintf(stderr, "pemu jcache: VERIFY MISMATCH pc=%#llx: %s (word %u: stored %08x, fresh %08x)\n", (unsigned long long) r->pc, why, at,
            why[0] == 'w' && at < r->nw ? tmp[at] : 0u, at < nw ? start[at] : 0u);
}
static void jc_append(const u8 *b, size_t n) {
    int fd = host_open(g_jc_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    struct stat st;
    if (host_fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) || flock(fd, LOCK_EX)) { host_close(fd); return; }
    if (host_fstat(fd, &st) == 0 && (u64) st.st_size + n <= JC_MAXFILE) {
        int ok = 1;
        if (st.st_size == 0) { u64 h[JC_HDR / 8] = { JC_FILEMAGIC, g_jc_binid }; ok = host_write(fd, h, JC_HDR) == (ssize_t) JC_HDR; }
        else { u64 h[2]; ok = st.st_size >= (off_t) JC_HDR && host_pread(fd, h, 16, 0) == 16 && h[0] == JC_FILEMAGIC && h[1] == g_jc_binid; }
        if (ok) {
            size_t o = 0;
            while (o < n) { ssize_t w = host_write(fd, b + o, n - o); if (w < 0 && errno == EINTR) continue; if (w <= 0) break; o += (size_t) w; }
            if (o == n) jst_cbytes += n;                          /* a short write leaves a torn record: readers skip it (never truncated: other processes have the file mapped) */
        }
    }
    flock(fd, LOCK_UN); host_close(fd);
}
static void jcache_flush(void) {                                  /* the blocks translated so far go to the file */
    if (!g_jpers || !jc_wlen) return;
    pthread_mutex_lock(&jmu);
    u8 *b = jc_wbuf; size_t n = jc_wlen;
    jc_wbuf = NULL; jc_wlen = jc_wcap = 0; jc_wreq = 0;
    pthread_mutex_unlock(&jmu);
    if (b) { jc_append(b, n); free(b); }
}
static void jcache_atfork_child(void) { jc_wbuf = NULL; jc_wlen = jc_wcap = 0; jc_wreq = 0; }   /* the parent writes what was pending at the fork */

/* ---- set-up */
static void jit_cache_dir(const char *d) {                       /* before jit_init(): NULL / "" = off */
    g_jc_dir[0] = 0;
    if (!d || !*d) return;
    size_t l = strlen(d);
    if (d[0] != '/' || l >= sizeof g_jc_dir - 64) { PEMU_NEG("error", 2, "PEMU_JIT_CACHE must be an absolute path (got %.100s): the translation cache is off", d); return; }
    memcpy(g_jc_dir, d, l + 1);
    while (l > 1 && g_jc_dir[l - 1] == '/') g_jc_dir[--l] = 0;
}
static void jcache_init(void) {
    if (!g_jc_dir[0]) return;
    if (!__executable_start || !etext) { PEMU_NEG("error", 3, "PEMU_JIT_CACHE: this toolchain gives no image bounds, the translation cache is off"); return; }
    struct stat st;
    if ((host_mkdir(g_jc_dir, 0700) && errno != EEXIST) || host_stat(g_jc_dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 022)) {
        PEMU_NEG("error", 4, "PEMU_JIT_CACHE %.100s needs a directory of yours, closed to others: cache off", g_jc_dir); return;
    }
    int fd = host_open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    void *m = MAP_FAILED; struct stat es;
    if (fd >= 0 && host_fstat(fd, &es) == 0 && es.st_size > 0) m = host_mmap(NULL, (size_t) es.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (fd >= 0) host_close(fd);
    if (m == MAP_FAILED) { PEMU_NEG("error", 5, "PEMU_JIT_CACHE: cannot read the emulator binary to identify it, the translation cache is off"); return; }
    g_jc_binid = jc_hash(m, (size_t) es.st_size) ^ 1ULL;
    host_munmap(m, (size_t) es.st_size);
    snprintf(g_jc_path, sizeof g_jc_path, "%s/jit-%016llx.pjc", g_jc_dir, (unsigned long long) g_jc_binid);
    g_jc_verify = getenv("PEMU_JIT_CACHE_VERIFY") != NULL;
    jc_load();
    g_jpers = 1;
}

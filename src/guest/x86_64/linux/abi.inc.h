/* src/guest/x86_64/linux/abi.inc.h - x86_64 guest ABI: open flags, struct stat, MAP_32BIT. SPDX-License-Identifier: Apache-2.0 */
#include "../../../os/linux/brk.inc.h"
struct fl { long g, h; };
static const struct fl oflags[] = {
    {0100, O_CREAT}, {0200, O_EXCL}, {0400, O_NOCTTY}, {01000, O_TRUNC}, {02000, O_APPEND}, {04000, O_NONBLOCK},
    {010000, O_DSYNC}, {020000, O_ASYNC}, {040000, O_DIRECT}, {0200000, O_DIRECTORY}, {0400000, O_NOFOLLOW},
    {01000000, O_NOATIME}, {02000000, O_CLOEXEC}, {04000000, O_SYNC & ~O_DSYNC}, {010000000, O_PATH},
};
static long fl_g2h(long f) {
    long r = f & 3;
    for (unsigned k = 0; k < sizeof oflags / sizeof *oflags; k++) if ((f & oflags[k].g) == oflags[k].g) r |= oflags[k].h;
    if ((f & 020200000) == 020200000) r |= O_TMPFILE;
    return r;
}
static long fl_h2g(long f) {
    long r = f & 3;
    for (unsigned k = 0; k < sizeof oflags / sizeof *oflags; k++) if ((f & oflags[k].h) == oflags[k].h && oflags[k].h) r |= oflags[k].g;
    return r;
}
static void cv_stat(const struct stat*s, u8*g, const struct pemu_own_ov*ov) {   /* ov: fake owner of the file, or NULL */
    memset(g, 0, 144);
    *(u64*)(g + 0) = s->st_dev; *(u64*)(g + 8) = s->st_ino; *(u64*)(g + 16) = s->st_nlink;
    *(u32*)(g + 24) = s->st_mode; *(u32*)(g + 28) = ov && ov->valid ? ov->uid : PEMU_UID(s->st_uid); *(u32*)(g + 32) = ov && ov->valid ? ov->gid : PEMU_GID(s->st_gid);
    *(u64*)(g + 40) = s->st_rdev; *(s64*)(g + 48) = s->st_size; *(s64*)(g + 56) = s->st_blksize; *(s64*)(g + 64) = s->st_blocks;
    *(s64*)(g + 72) = s->st_atim.tv_sec; *(s64*)(g + 80) = s->st_atim.tv_nsec;
    *(s64*)(g + 88) = s->st_mtim.tv_sec; *(s64*)(g + 96) = s->st_mtim.tv_nsec;
    *(s64*)(g + 104) = s->st_ctim.tv_sec; *(s64*)(g + 112) = s->st_ctim.tv_nsec;
}
static long g_stat(long dirfd, const char*p, u8*out, long flags) {
    struct stat st;
    struct pemu_own_ov ov = {0};
    long r = pemu_callat_ov(HOP_newfstatat, dirfd, p, !(flags & 0x100), (long) &st, flags, 0, 0, &ov);
    if (r == 0) cv_stat(&st, out, &ov);
    return r;
}
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
/* MAP_32BIT: x86-64 guests ask for an address in the first 2 GiB (JIT code, some allocators). The aarch64 host has no such
 * flag, so look for a free range there ourselves; the guest is mapped 1:1, so any address that mmap accepts is valid. */
static long mmap32(u64 len, u64 prot, u64 flags, u64 fd, u64 off) {
    static u64 next = 0x10000000ULL;
    const u64 lo = 0x10000000ULL, hi = 0x80000000ULL, pg = (u64) sysconf(_SC_PAGESIZE);
    u64 n = (len + pg - 1) & ~(pg - 1), a = __atomic_load_n(&next, __ATOMIC_RELAXED);
    if (!n) return -EINVAL;
    for (int k = 0; k < 8192; k++) {
        if (a < lo || a + n > hi) a = lo;
        long r = hs(HOP_mmap, (long) a, (long) len, (long) prot, (long) (flags | MAP_FIXED_NOREPLACE), (long) fd, (long) off);
        if (r == (long) a) { __atomic_store_n(&next, a + n, __ATOMIC_RELAXED); return r; }
        if (r >= 0) host_munmap((void*) r, n);                 /* a kernel without MAP_FIXED_NOREPLACE took it as a hint */
        else if (r != -EEXIST) return r;
        a += n < 0x10000 ? 0x10000 : n;
    }
    return -ENOMEM;
}


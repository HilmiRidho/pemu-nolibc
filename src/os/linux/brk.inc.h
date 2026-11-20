/* src/os/linux/brk.inc.h - brk for every guest (included by guest/<arch>/linux/abi.inc.h).
 *
 * The guest heap is one 1 GiB anonymous mapping that is always readable and writable; brk only moves brk_cur inside it.  A real kernel
 * unmaps the pages above the new break when brk shrinks, so they are zero again when the break grows back.  glibc depends on that:
 * calloc() does not clear memory that malloc just got from sbrk(), because "fresh from the kernel" means zero.  Without the scrub below
 * a shrink (glibc malloc trim, which every large free() of the top chunk does) followed by a growth hands stale data out of calloc():
 * GCC's hash tables (htab_create, xcalloc) then hold garbage pointers and free() crashes (internal compiler error: Segmentation fault
 * in htab_delete / __libc_free). */
static u64 brk_base, brk_cur, brk_end;
static void pemu_brk_scrub(u64 lo, u64 hi) {             /* zero [lo, hi) of the heap region */
    if (hi <= lo) return;
    u64 pg = (u64) sysconf(_SC_PAGESIZE);                /* host page: 4 KiB, but 16 KiB / 64 KiB on some aarch64 hosts */
    if (!pg || (pg & (pg - 1))) pg = 4096;
    u64 a = (lo + pg - 1) & ~(pg - 1), b = hi & ~(pg - 1);  /* the whole host pages inside the range */
    if (a >= b) { memset((void*) lo, 0, (size_t) (hi - lo)); return; }
    if (lo < a) memset((void*) lo, 0, (size_t) (a - lo));
    if (host_madvise((void*) a, (size_t) (b - a), MADV_DONTNEED) != 0) memset((void*) a, 0, (size_t) (b - a));   /* private anonymous: reads zero again */
    if (hi > b) memset((void*) b, 0, (size_t) (hi - b));
}

static long pemu_brk(u64 a0) {
    if (a0 >= brk_base && a0 <= brk_end) {
        u64 old = brk_cur;
        /* guest pages are 4 KiB (AT_PAGESZ): the kernel releases the pages from PAGE_ALIGN(new) to PAGE_ALIGN(old) */
        u64 lo = (a0 + 4095) & ~4095ULL, hi = (old + 4095) & ~4095ULL;
        if (hi > brk_end) hi = brk_end;
        if (a0 < old) pemu_brk_scrub(lo, hi);
        brk_cur = a0;
    }
    return (long) brk_cur;
}

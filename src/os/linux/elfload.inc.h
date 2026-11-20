/* src/os/linux/elfload.inc.h - ELF loading, shared by every 64-bit guest. SPDX-License-Identifier: Apache-2.0
 * The guest's own linux/guest.inc.h must define before this file: PEMU_ELF_MACHINE (EM_*), PEMU_ELF_NAME ("x86_64") and PEMU_ELF_NOTE_RWX(start, len)
 * (what to do with a segment that is both writable and executable: the x86_64 JIT never translates it, the code may modify itself). */
/* ------------------------------------------------------------ ELF loading */
#include "elf_check.inc.h"
typedef struct { u64 entry, phdr, phnum, base; char interp[4096]; int has_interp; } ElfInfo;

static int load_elf(const char*path, ElfInfo*ei) {
    int fd = host_open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { pemu_err("pemu: %s: %s\n", path, strerror(errno)); return -1; }
    Elf64_Ehdr eh;
    if (host_pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.e_ident, "\177ELF", 4) || eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != PEMU_ELF_MACHINE) {
        pemu_err("pemu: %s: not an %s ELF executable\n", path, PEMU_ELF_NAME); host_close(fd); return -1;
    }
    Elf64_Phdr*ph = malloc((size_t) eh.e_phnum * sizeof *ph);
    if (!ph && eh.e_phnum) { host_close(fd); return -1; }
    if (host_pread(fd, ph, (size_t) eh.e_phnum * sizeof *ph, (off_t) eh.e_phoff) != (ssize_t)(eh.e_phnum * sizeof *ph)) { free(ph); host_close(fd); return -1; }
    if (elf_check(&eh, ph, path) < 0) { free(ph); host_close(fd); return -1; }
    u64 lo = ~0ULL, hi = 0;
    for (int k = 0; k < eh.e_phnum; k++) if (ph[k].p_type == PT_LOAD) {
        if (ph[k].p_vaddr < lo) lo = ph[k].p_vaddr;
        if (ph[k].p_vaddr + ph[k].p_memsz > hi) hi = ph[k].p_vaddr + ph[k].p_memsz;
    }
    lo &= ~4095ULL; hi = (hi + 4095) & ~4095ULL;
    u64 base = 0;
    if (eh.e_type == ET_DYN) {
        void*mm = host_mmap(NULL, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (mm == MAP_FAILED) { pemu_err("pemu: mmap: %s\n", strerror(errno)); free(ph); host_close(fd); return -1; }
        base = (u64) mm - lo;
    }
    u64 phdr_addr = 0;
    u64 prev_s = 0, prev_e = 0; int have_prev = 0;      /* page range of the previous PT_LOAD */
    for (int k = 0; k < eh.e_phnum; k++) {
        Elf64_Phdr*p = &ph[k];
        if (p->p_type == PT_INTERP && host_pread(fd, ei->interp, p->p_filesz < 4095 ? p->p_filesz : 4095, (off_t) p->p_offset) > 0) ei->has_interp = 1;
        if (p->p_type == PT_PHDR) phdr_addr = base + p->p_vaddr;
        if (p->p_type != PT_LOAD) continue;
        u64 s = (base + p->p_vaddr) & ~4095ULL, e = (base + p->p_vaddr + p->p_memsz + 4095) & ~4095ULL;
        /* A page shared with the end of the previous segment (hand-made files, ld -N) is already mapped and holds that segment's tail:
         * map only the pages after it, or the anonymous mmap would replace the page and wipe the tail. */
        int shared = have_prev && s >= prev_s && s < prev_e;
        u64 ms = shared ? prev_e : s;
        if (!shared || e > ms) {
            void*mm = host_mmap((void*) ms, e - ms, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (mm == MAP_FAILED) { pemu_err("pemu: mmap segment: %s\n", strerror(errno)); free(ph); host_close(fd); return -1; }
        }
        if (shared) host_mprotect((void*) s, (e < prev_e ? e : prev_e) - s, PROT_READ | PROT_WRITE);   /* the previous segment may have made it read-only */
        if (p->p_filesz && host_pread(fd, (void*)(base + p->p_vaddr), p->p_filesz, (off_t) p->p_offset) != (ssize_t) p->p_filesz) { pemu_err("pemu: short read\n"); free(ph); host_close(fd); return -1; }
        int prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0);
        if ((prot & PROT_WRITE) && (prot & PROT_EXEC)) PEMU_ELF_NOTE_RWX(s, e - s);
        host_mprotect((void*) s, e - s, prot | PROT_READ);
        if (!shared || e > prev_e) prev_e = e;
        prev_s = s; have_prev = 1;
        if (!phdr_addr && p->p_offset <= eh.e_phoff && eh.e_phoff < p->p_offset + p->p_filesz) phdr_addr = base + p->p_vaddr + (eh.e_phoff - p->p_offset);
    }
    ei->entry = base + eh.e_entry; ei->phdr = phdr_addr; ei->phnum = eh.e_phnum; ei->base = base;
    free(ph); host_close(fd);
    return 0;
}
static u64 push_str(u64*sp, const char*s) { size_t n = strlen(s) + 1; *sp -= n; memcpy((void*)*sp, s, n); return *sp; }


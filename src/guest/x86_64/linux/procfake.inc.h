/* src/guest/x86_64/linux/procfake.inc.h - x86_64 /proc/cpuinfo, /proc/self/exe patching, legacy getdents. SPDX-License-Identifier: Apache-2.0 */
/* /proc/cpuinfo as an x86 CPU. The host's file lists ARM flags (asimd, fp, ...), so a guest that decides what the CPU can do by
 * parsing it finds no sse3/pni and refuses to run. The text is consistent with cpuid (see cpuid()). */
static const char cpuinfo_flags[] = "fpu tsc cx8 cmov mmx fxsr sse sse2 syscall nx lm constant_tsc nopl pni ssse3 cx16 sse4_1 sse4_2 popcnt lahf_lm";
static long cpuinfo_fd(long hostflags) {
    long n = sysconf(_SC_NPROCESSORS_ONLN); if (n < 1) n = 1; if (n > 256) n = 256;
    size_t cap = (size_t) n * 1100 + 64, len = 0; char*b = malloc(cap);
    if (!b) return -ENOMEM;
    for (int i = 0; i < n; i++)
        len += (size_t) snprintf(b + len, cap - len,
            "processor\t: %d\nvendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 26\nmodel name\t: " PEMU_BRAND "\n"
            "stepping\t: 5\ncpu MHz\t\t: " PEMU_MHZ_STR "\ncache size\t: 8192 KB\nphysical id\t: 0\nsiblings\t: %ld\ncore id\t\t: %d\ncpu cores\t: %ld\n"
            "apicid\t\t: %d\ninitial apicid\t: %d\nfpu\t\t: yes\nfpu_exception\t: yes\ncpuid level\t: 4\nwp\t\t: yes\nflags\t\t: %s\n"
            "bogomips\t: " PEMU_BOGO_STR "\nclflush size\t: 64\ncache_alignment\t: 64\naddress sizes\t: 40 bits physical, 48 bits virtual\npower management:\n\n",
            i, n, i, n, i, i, cpuinfo_flags);
    int cloexec = (hostflags & O_CLOEXEC) != 0; long fd = -1;
#ifdef HOP_memfd_create
    fd = host_syscall_e(HOP_memfd_create, "pemu-cpuinfo", cloexec ? 1 /*MFD_CLOEXEC*/ : 0);
    if (fd >= 0) {
        if (host_write((int) fd, b, len) != (ssize_t) len || host_lseek((int) fd, 0, SEEK_SET) < 0) { host_close((int) fd); fd = -1; }
    }
#endif
    if (fd < 0 && len <= 60000) {                       /* no memfd (blocked, old kernel): a pipe is enough for reading it once */
        int p[2];
        if (host_pipe2(p, cloexec ? O_CLOEXEC : 0) == 0) {
            if (host_write(p[1], b, len) == (ssize_t) len) { host_close(p[1]); fd = p[0]; } else { host_close(p[0]); host_close(p[1]); }
        }
    }
    free(b);
    return fd >= 0 ? fd : -EACCES;
}
/* files the emulator serves itself: /proc/cpuinfo and the guest's own /proc/self/cmdline */
static int is_cpuinfo(const char*p, long hostflags) { return p && (hostflags & O_ACCMODE) == O_RDONLY && (!strcmp(p, "/proc/cpuinfo") || is_cmdline(p)); }
static long fake_proc_fd(const char*p, long hostflags) { return is_cmdline(p) ? cmdline_fd(hostflags) : cpuinfo_fd(hostflags); }
/* syscalls whose path argument may name /proc/self/exe: the path is swapped for the program's own, so exec, open and stat see the guest file */
static __thread char g_sxbuf[PATH_MAX];
static int selfexe_patch(u64 n, u64*a0, u64*a1) {
    u64*pp = NULL;
    switch (n) {
    case 2: case 4: case 6: case 21: case 59: pp = a0; break;
    case 257: case 262: case 269: case 322: case 332: case 437: case 439: pp = a1; break;
    default: return 0;
    }
    if (!*pp || !is_selfexe((const char*) *pp)) return 0;
    snprintf(g_sxbuf, sizeof g_sxbuf, "%s", pemu_exe); *pp = (u64)(uintptr_t) g_sxbuf; return 1;
}

/* getdents(2): the host has only getdents64. A struct linux_dirent record has the same length as a linux_dirent64 one (the name
 * starts one byte earlier and d_type moves to the last byte of the record), so the host buffer is converted in place. */
static long g_getdents(long fd, u8*buf, long count) {
    long r = hs(HOP_getdents64, fd, (long) buf, count, 0, 0, 0);
    for (long off = 0; r > 0 && off < r; ) {
        u8*p = buf + off; u32 len = *(u16u*)(p + 16); u8 type = p[18];
        if (len < 24 || off + len > r) break;
        memmove(p + 18, p + 19, strlen((char*) p + 19) + 1);
        p[len - 1] = type;
        off += len;
    }
    return r;
}


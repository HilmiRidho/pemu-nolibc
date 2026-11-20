/* src/os/linux/procself.inc.h - the guest's own identity in /proc (exe, cmdline); shared by every guest. SPDX-License-Identifier: Apache-2.0 */
/* ---- the guest's own identity in /proc: exe and cmdline.
 * /proc/self/exe on the host is the EMULATOR, and /proc/self/cmdline is the emulator's command line. A guest that finds its own files
 * through them (Electron / Chromium: resources, icudtl.dat, *.pak, and every child process it starts is exec'd as /proc/self/exe) then
 * runs the emulator without a program, or cannot find its data. pemu_exe is the guest path of the running program: with a rootfs it is
 * a guest path (and the proot hook already answers readlink), without one it is the host path of the program (set in main). */
static int g_exe_ok;                                       /* pemu_exe names the program (not a memfd / deleted file handed over by --exec-fd) */
static u64 g_arg_lo, g_arg_hi;                             /* the guest's argv strings on its stack; /proc/self/cmdline is read from here, like the kernel does */
static int is_selfproc(const char*p, const char*leaf) {    /* "/proc/self/<leaf>" or "/proc/<this pid>/<leaf>" */
    char pre[48]; size_t n;
    if (!p || strncmp(p, "/proc/", 6)) return 0;
    if (!strncmp(p, "/proc/self/", 11)) return !strcmp(p + 11, leaf);
    n = (size_t) snprintf(pre, sizeof pre, "/proc/%d/", (int) host_getpid());
    return !strncmp(p, pre, n) && !strcmp(p + n, leaf);
}
static int is_selfexe(const char*p) { return g_exe_ok && pemu_exe[0] && is_selfproc(p, "exe"); }
static int is_cmdline(const char*p) { return g_arg_hi > g_arg_lo && is_selfproc(p, "cmdline"); }
/* a read-only fd holding `len` bytes (memfd, or a pipe when memfd is blocked) */
static long bytes_fd(const char*name, const void*b, size_t len, long hostflags) {
    int cloexec = (hostflags & O_CLOEXEC) != 0; long fd = -1;
#ifdef HOP_memfd_create
    fd = host_syscall_e(HOP_memfd_create, name, cloexec ? 1 /*MFD_CLOEXEC*/ : 0);
    if (fd >= 0 && (host_write((int) fd, b, len) != (ssize_t) len || host_lseek((int) fd, 0, SEEK_SET) < 0)) { host_close((int) fd); fd = -1; }
#endif
    if (fd < 0 && len <= 60000) {
        int pp[2];
        if (host_pipe2(pp, cloexec ? O_CLOEXEC : 0) == 0) {
            if (host_write(pp[1], b, len) == (ssize_t) len) { host_close(pp[1]); fd = pp[0]; } else { host_close(pp[0]); host_close(pp[1]); }
        }
    }
    return fd >= 0 ? fd : -EACCES;
}
static long cmdline_fd(long hostflags) { return bytes_fd("pemu-cmdline", (const void*) g_arg_lo, (size_t)(g_arg_hi - g_arg_lo), hostflags); }
/* readlink of the guest's own exe without a rootfs (with one, pemu_readlinkat does it) */
static long selfexe_readlink(char*buf, long sz) { long l = (long) strlen(pemu_exe); if (sz < 0) return -EINVAL; if (l > sz) l = sz; memcpy(buf, pemu_exe, (size_t) l); return l; }

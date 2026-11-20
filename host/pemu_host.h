/* host/pemu_host.h - the one place where pemu asks the host kernel for something.  SPDX-License-Identifier: Apache-2.0
 *
 * Step 1 of the road to a pemu that needs no host kernel: every call that leaves the emulator for the host goes through a
 * host_*() function here.  Nothing changes in behaviour: each function does what the call it replaced did, and counts it.
 * Later steps replace the bodies (step 2: no libc, step 3: guest memory out of the host's address space, step 4: parts of the
 * kernel inside pemu) without touching the callers.
 *
 *   host_syscall()    a host operation by name (HOP_<name>, host/pemu_hop.h; on Linux its number is the kernel's), returns -errno.  hs() in src/guest/x86_64/linux/host_syscall.inc.h is this (tests define their own hs()).
 *   host_syscall_e()  the same, libc style (-1 and errno), for places that used syscall() directly.
 *   host_mmap / host_munmap / host_mprotect / host_madvise    the memory calls of the emulator itself (JIT cache, brk, ELF loader, stack): syscalls, no libc.
 *   host_open / host_openat / host_close / host_read / host_write / host_pread / host_lseek / host_stat / host_lstat / host_fstat / host_fstatat    its own files.
 *   host_fork / host_waitpid / host_thread_create / host_thread_exit    processes and threads.
 *   host_getpid / host_kill / host_raise / host_exit / host_mkdir / host_unlink / host_rmdir / host_rename / host_chdir / host_getcwd / host_dup2 / host_pipe2 / host_fcntl
 *
 * PEMU_HOST_STATS=/abs/path (a file per process, <path>.<pid>) or PEMU_HOST_STATS=1 (stderr) prints, when the guest exits, how often each
 * host call was made.  This is the number the road map drives to zero.  NOT counted yet: the libc calls pemu makes for itself that are not
 * behind this header (signals, stdio, execvp, getenv, ...); they move behind it in step 2.  Counted: open, read, write, pread,
 * lseek, close, stat, lstat, fstat.
 *
 * tools/check_host_seam.py fails the build when code outside this file calls mmap / fork / syscall ... directly again. */
#ifndef PEMU_HOST_H
#define PEMU_HOST_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "pemu_hop.h"            /* HOP_<name>: the host operations src/ asks for; this layer turns them into what the host kernel knows */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- counters: one slot per host syscall number, then the process / thread calls that are not a plain syscall */
#define HOST_NR_MAX 512
enum { HK_BIG = HOST_NR_MAX, HK_FORK, HK_WAIT, HK_THREAD, HK_N };      /* HK_BIG: a number >= HOST_NR_MAX */
static unsigned long g_host_cnt[HK_N];
static inline void host_count(long nr) {
    unsigned long i = (unsigned long) nr < (unsigned long) HOST_NR_MAX ? (unsigned long) nr : (unsigned long) HK_BIG;
    __atomic_fetch_add(&g_host_cnt[i], 1UL, __ATOMIC_RELAXED);
}

/* ---- the syscall itself: inline asm, no libc (step 2).  host_raw returns the kernel's value (-errno on failure). */
#if defined(__x86_64__)
static inline long host_raw(long nr, long a, long b, long c, long d, long e, long f) {
    long r; register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
#elif defined(__aarch64__)
static inline long host_raw(long nr, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = nr; register long x0 __asm__("x0") = a; register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c; register long x3 __asm__("x3") = d; register long x4 __asm__("x4") = e; register long x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}
#else
#error "pemu host: only x86_64 and aarch64 hosts"
#endif
/* host_syscall(HOP_x, ...): the call src/ makes.  The operation name becomes the kernel's number here and only here (HOP_NR); the counters
 * and the stats names are the kernel's numbers.  host_raw() / host_sys_e() below take a kernel number: they are for this layer and host/rt/. */
static inline long host_syscall(long op, long a, long b, long c, long d, long e, long f) {
    long nr = HOP_NR(op);
    host_count(nr);
    return host_raw(nr, a, b, c, d, e, f);
}
/* libc style result: -1 and errno (errno itself is still libc's until the TLS step) */
static inline long host_ret(long r) { if ((unsigned long) r > (unsigned long) -4096L) { errno = (int) -r; return -1; } return r; }
/* variadic like syscall(): 1 to 7 arguments, pointers allowed; evaluates nr once */
#define host_syscall_e(op, ...) ({ long hn_ = HOP_NR((long) (op)); host_count(hn_); host_ret(host_arg_(hn_, ##__VA_ARGS__, 0L, 0L, 0L, 0L, 0L, 0L)); })
#define host_sys_e(nr, ...) ({ long hn_ = (long) (nr); host_count(hn_); host_ret(host_arg_(hn_, ##__VA_ARGS__, 0L, 0L, 0L, 0L, 0L, 0L)); })   /* a kernel number, not a HOP_: this layer's own calls */
#define host_arg_(nr, a, b, c, d, e, f, ...) host_raw((nr), (long) (a), (long) (b), (long) (c), (long) (d), (long) (e), (long) (f))

#ifndef PEMU_UNIT_TEST_LIBC
#include "rt/pemu_rt.h"            /* the functions pemu used from libc, defined here (libc names); there is no libc: not linked, not even its headers */
#endif                             /* PEMU_UNIT_TEST_LIBC: only the host-side unit tests (tests/unit/test_*.c) set it: they drive src/proot/ with sockets, system(), pthread barriers ... of the system's libc */

/* ---- memory of the emulator itself: the syscalls themselves, libc style (-1 and errno; MAP_FAILED is (void*) -1) */
static inline void *host_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    return (void *) host_sys_e(SYS_mmap, (long) addr, (long) len, (long) prot, (long) flags, (long) fd, (long) off);
}
static inline int host_munmap(void *addr, size_t len) { return (int) host_sys_e(SYS_munmap, (long) addr, (long) len); }
static inline int host_mprotect(void *addr, size_t len, int prot) { return (int) host_sys_e(SYS_mprotect, (long) addr, (long) len, (long) prot); }
static inline int host_madvise(void *addr, size_t len, int adv) { return (int) host_sys_e(SYS_madvise, (long) addr, (long) len, (long) adv); }

/* ---- files of the emulator itself (its own rootfs files, log, ELF loader, proc reads): raw syscalls, -1 and errno */
#include <stdarg.h>
#include <sys/stat.h>
static inline int host_openat(int dfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    return (int) host_sys_e(SYS_openat, (long) dfd, (long) path, (long) flags, (long) mode);
}
static inline int host_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    return host_openat(AT_FDCWD, path, flags, mode);
}
static inline int host_close(int fd) { return (int) host_sys_e(SYS_close, (long) fd); }
static inline ssize_t host_read(int fd, void *buf, size_t n) { return (ssize_t) host_sys_e(SYS_read, (long) fd, (long) buf, (long) n); }
static inline ssize_t host_write(int fd, const void *buf, size_t n) { return (ssize_t) host_sys_e(SYS_write, (long) fd, (long) buf, (long) n); }
static inline ssize_t host_pread(int fd, void *buf, size_t n, off_t off) { return (ssize_t) host_sys_e(SYS_pread64, (long) fd, (long) buf, (long) n, (long) off); }
static inline off_t host_lseek(int fd, off_t off, int whence) { return (off_t) host_sys_e(SYS_lseek, (long) fd, (long) off, (long) whence); }
static inline int host_fstatat(int dfd, const char *path, struct stat *st, int flags) { return (int) host_sys_e(SYS_newfstatat, (long) dfd, (long) path, (long) st, (long) flags); }
static inline int host_stat(const char *path, struct stat *st) { return host_fstatat(AT_FDCWD, path, st, 0); }
static inline int host_lstat(const char *path, struct stat *st) { return host_fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW); }
static inline int host_fstat(int fd, struct stat *st) { return (int) host_sys_e(SYS_fstat, (long) fd, (long) st); }

/* ---- processes and threads.  fork() and pthread_create() are host/rt/rt_core.h's own (libc names, no libc): the callers rely on its atfork handlers and TLS setup. */
static inline pid_t host_fork(void) { __atomic_fetch_add(&g_host_cnt[HK_FORK], 1UL, __ATOMIC_RELAXED); return fork(); }
static inline pid_t host_waitpid(pid_t pid, int *st, int opt) { __atomic_fetch_add(&g_host_cnt[HK_WAIT], 1UL, __ATOMIC_RELAXED); return (pid_t) host_ret(host_raw(SYS_wait4, pid, (long) st, opt, 0, 0, 0)); }
static inline int host_thread_create(pthread_t *th, const pthread_attr_t *at, void *(*fn)(void *), void *arg) {
    __atomic_fetch_add(&g_host_cnt[HK_THREAD], 1UL, __ATOMIC_RELAXED); return pthread_create(th, at, fn, arg);
}
static inline void host_thread_exit(void) { pthread_exit(NULL); }

/* ---- small process / file-system calls of the emulator itself: raw syscalls, -1 and errno */
#include <signal.h>
static inline pid_t host_getpid(void) { return (pid_t) host_sys_e(SYS_getpid); }
static inline int host_kill(pid_t pid, int sig) { return (int) host_sys_e(SYS_kill, (long) pid, (long) sig); }
static inline int host_raise(int sig) { return (int) host_sys_e(SYS_tgkill, host_raw(SYS_getpid, 0, 0, 0, 0, 0, 0), host_raw(SYS_gettid, 0, 0, 0, 0, 0, 0), (long) sig); }
static inline __attribute__((noreturn)) void host_exit(int code) { host_count(SYS_exit_group); for (;;) host_raw(SYS_exit_group, code, 0, 0, 0, 0, 0); }
static inline int host_mkdir(const char *path, mode_t mode) { return (int) host_sys_e(SYS_mkdirat, (long) AT_FDCWD, (long) path, (long) mode); }
static inline int host_unlink(const char *path) { return (int) host_sys_e(SYS_unlinkat, (long) AT_FDCWD, (long) path, 0L); }
static inline int host_rmdir(const char *path) { return (int) host_sys_e(SYS_unlinkat, (long) AT_FDCWD, (long) path, (long) AT_REMOVEDIR); }
static inline int host_rename(const char *from, const char *to) { return (int) host_sys_e(SYS_renameat2, (long) AT_FDCWD, (long) from, (long) AT_FDCWD, (long) to, 0L); }
static inline int host_chdir(const char *path) { return (int) host_sys_e(SYS_chdir, (long) path); }
static inline char *host_getcwd(char *buf, size_t n) { return host_sys_e(SYS_getcwd, (long) buf, (long) n) < 0 ? NULL : buf; }
static inline int host_dup2(int from, int to) {
    if (from == to) return (int) host_sys_e(SYS_fcntl, (long) from, (long) F_GETFD) < 0 ? -1 : to;   /* dup3 refuses from == to */
    return (int) host_sys_e(SYS_dup3, (long) from, (long) to, 0L);
}
static inline int host_pipe2(int fds[2], int flags) { return (int) host_sys_e(SYS_pipe2, (long) fds, (long) flags); }
static inline int host_fcntl(int fd, int cmd, long arg) { return (int) host_sys_e(SYS_fcntl, (long) fd, (long) cmd, arg); }

/* ---- statistics */
static const char *host_nr_name(long nr) {
    switch (nr) {
#define HN(x) case SYS_##x: return #x;
    HN(read) HN(write) HN(openat) HN(close) HN(lseek) HN(mmap) HN(mprotect) HN(munmap) HN(madvise) HN(brk) HN(rt_sigaction)
    HN(rt_sigprocmask) HN(ioctl) HN(pread64) HN(pwrite64) HN(readv) HN(writev) HN(pipe2) HN(dup3) HN(fcntl) HN(futex) HN(clone)
    HN(execve) HN(exit_group) HN(wait4) HN(kill) HN(tkill) HN(tgkill) HN(getpid) HN(gettid) HN(getuid) HN(getcwd) HN(chdir)
    HN(readlinkat) HN(unlinkat) HN(mkdirat) HN(faccessat) HN(newfstatat) HN(statx) HN(clock_gettime) HN(getrandom)
    HN(memfd_create) HN(socket) HN(connect) HN(sendto) HN(recvfrom) HN(prlimit64) HN(sched_yield) HN(ppoll) HN(openat2)
    HN(getdents64) HN(set_tid_address) HN(process_vm_readv) HN(fstat) HN(renameat2)
#ifdef SYS_open
    HN(open) HN(stat) HN(lstat) HN(access) HN(pipe) HN(dup2) HN(readlink) HN(unlink) HN(mkdir) HN(poll) HN(arch_prctl)
#endif
#undef HN
    }
    return "?";
}
static void host_stats_dump(void) {
    const char *sp = getenv("PEMU_HOST_STATS");
    if (!sp || !sp[0]) return;
    char buf[4096]; size_t o = 0; unsigned long total = 0, kinds = 0;
    for (int i = 0; i < HK_N; i++) { unsigned long n = __atomic_load_n(&g_host_cnt[i], __ATOMIC_RELAXED); total += n; kinds += n != 0; }
    o += (size_t) snprintf(buf + o, sizeof buf - o, "pemu host: %lu calls into the host kernel, %lu kinds\n", total, kinds);
    for (int i = 0; i < HK_N && o + 80 < sizeof buf; i++) {
        unsigned long n = __atomic_load_n(&g_host_cnt[i], __ATOMIC_RELAXED);
        if (!n) continue;
        if (i == HK_BIG) o += (size_t) snprintf(buf + o, sizeof buf - o, "pemu host: %8lu x  (syscall number >= %d)\n", n, HOST_NR_MAX);
        else if (i == HK_FORK) o += (size_t) snprintf(buf + o, sizeof buf - o, "pemu host: %8lu x  fork()\n", n);
        else if (i == HK_WAIT) o += (size_t) snprintf(buf + o, sizeof buf - o, "pemu host: %8lu x  waitpid()\n", n);
        else if (i == HK_THREAD) o += (size_t) snprintf(buf + o, sizeof buf - o, "pemu host: %8lu x  thread create\n", n);
        else o += (size_t) snprintf(buf + o, sizeof buf - o, "pemu host: %8lu x  %s (%d)\n", n, host_nr_name(i), i);
    }
    int fd = 2;
    if (sp[0] == '/') {
        char fn[600]; snprintf(fn, sizeof fn, "%s.%d", sp, (int) host_getpid());
        fd = host_open(fn, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (fd < 0) return;
    }
    ssize_t w = host_write(fd, buf, o); (void) w;
    if (fd != 2) host_close(fd);
}
#endif /* PEMU_HOST_H */

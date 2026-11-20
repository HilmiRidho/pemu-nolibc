/* host/rt/sys.h - small system calls with their libc names, directories, realpath, exec, time.  SPDX-License-Identifier: Apache-2.0 */
#ifndef PEMU_RT_SYS_H
#define PEMU_RT_SYS_H
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <sys/xattr.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

int open(const char *p, int fl, ...) { va_list ap; va_start(ap, fl); mode_t m = (fl & (O_CREAT | O_TMPFILE)) ? va_arg(ap, mode_t) : 0; va_end(ap); return (int) rt_sys(SYS_openat, AT_FDCWD, p, fl, m); }
ssize_t read(int fd, void *b, size_t n) { return (ssize_t) rt_sys(SYS_read, fd, b, n); }
ssize_t write(int fd, const void *b, size_t n) { return (ssize_t) rt_sys(SYS_write, fd, b, n); }
int close(int fd) { return (int) rt_sys(SYS_close, fd); }
int access(const char *p, int m) { return (int) rt_sys(SYS_faccessat, AT_FDCWD, p, m); }
int chmod(const char *p, mode_t m) { return (int) rt_sys(SYS_fchmodat, AT_FDCWD, p, m); }
int flock(int fd, int op) { return (int) rt_sys(SYS_flock, fd, op); }
int fchmod(int fd, mode_t m) { return (int) rt_sys(SYS_fchmod, fd, m); }
int fchmodat(int d, const char *p, mode_t m, int fl) {
    if (!fl) return (int) rt_sys(SYS_fchmodat, d, p, m);
    long r = rt_raw(452 /* fchmodat2 */, d, p, m, fl, 0, 0);
    if (r == -ENOSYS) r = -EOPNOTSUPP;
    if (r < 0) { errno = (int) -r; return -1; } return 0;
}
int mknod(const char *p, mode_t m, dev_t d) { return (int) rt_sys(SYS_mknodat, AT_FDCWD, p, m, d); }
ssize_t readlink(const char *p, char *b, size_t n) { return (ssize_t) rt_sys(SYS_readlinkat, AT_FDCWD, p, b, n); }
int symlink(const char *t, const char *l) { return (int) rt_sys(SYS_symlinkat, t, AT_FDCWD, l); }
int truncate(const char *p, off_t n) { return (int) rt_sys(SYS_truncate, p, n); }
int unlinkat(int d, const char *p, int fl) { return (int) rt_sys(SYS_unlinkat, d, p, fl); }
int pipe(int f[2]) { return (int) rt_sys(SYS_pipe2, f, 0); }
pid_t getppid(void) { return (pid_t) rt_raw(SYS_getppid, 0, 0, 0, 0, 0, 0); }
pid_t getpgrp(void) { return (pid_t) rt_raw(SYS_getpgid, 0, 0, 0, 0, 0, 0); }
uid_t getuid(void) { return (uid_t) rt_raw(SYS_getuid, 0, 0, 0, 0, 0, 0); }
uid_t geteuid(void) { return (uid_t) rt_raw(SYS_geteuid, 0, 0, 0, 0, 0, 0); }
gid_t getgid(void) { return (gid_t) rt_raw(SYS_getgid, 0, 0, 0, 0, 0, 0); }
gid_t getegid(void) { return (gid_t) rt_raw(SYS_getegid, 0, 0, 0, 0, 0, 0); }
int prctl(int op, ...) { va_list ap; va_start(ap, op); unsigned long a = va_arg(ap, unsigned long), b = va_arg(ap, unsigned long), c = va_arg(ap, unsigned long), d = va_arg(ap, unsigned long); va_end(ap); return (int) rt_sys(SYS_prctl, op, a, b, c, d); }
int sched_yield(void) { return (int) rt_sys(SYS_sched_yield); }
int sched_getaffinity(pid_t p, size_t n, cpu_set_t *s) { long r = rt_sys(SYS_sched_getaffinity, p, n, s); return r < 0 ? -1 : 0; }
int nanosleep(const struct timespec *a, struct timespec *b) { return (int) rt_sys(SYS_nanosleep, a, b); }
int usleep(useconds_t us) { struct timespec t = { (time_t) (us / 1000000), (long) (us % 1000000) * 1000 }; return nanosleep(&t, 0); }
unsigned sleep(unsigned s) { struct timespec t = { (time_t) s, 0 }; return nanosleep(&t, 0) ? 1 : 0; }
int clock_gettime(clockid_t c, struct timespec *t) {
    if (rt_vdso_cgt) { long r = rt_vdso_cgt((int) c, t); if (r != -ENOSYS) return (int) rt_ret_(r); }
    return (int) rt_sys(SYS_clock_gettime, c, t);
}
int gettimeofday(struct timeval *tv, void *tz) {
    if (rt_vdso_gtod) { long r = rt_vdso_gtod(tv, tz); if (r != -ENOSYS) return (int) rt_ret_(r); }
    return (int) rt_sys(SYS_gettimeofday, tv, tz);
}
time_t time(time_t *o) {
    if (rt_vdso_time) return (time_t) rt_vdso_time(o);
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t); if (o) *o = t.tv_sec; return t.tv_sec;
}
clock_t times(struct tms *b) { return (clock_t) rt_sys(SYS_times, b); }
int uname(struct utsname *u) { return (int) rt_sys(SYS_uname, u); }
int sysinfo(struct sysinfo *s) { return (int) rt_sys(SYS_sysinfo, s); }
ssize_t getrandom(void *b, size_t n, unsigned fl) { return (ssize_t) rt_sys(SYS_getrandom, b, n, fl); }
int getrlimit(__rlimit_resource_t r, struct rlimit *l) { return (int) rt_sys(SYS_prlimit64, 0, r, 0, l); }
int setxattr(const char *p, const char *n, const void *v, size_t s, int f) { return (int) rt_sys(SYS_setxattr, p, n, v, s, f); }
int lsetxattr(const char *p, const char *n, const void *v, size_t s, int f) { return (int) rt_sys(SYS_lsetxattr, p, n, v, s, f); }
int fsetxattr(int fd, const char *n, const void *v, size_t s, int f) { return (int) rt_sys(SYS_fsetxattr, fd, n, v, s, f); }
ssize_t getxattr(const char *p, const char *n, void *v, size_t s) { return (ssize_t) rt_sys(SYS_getxattr, p, n, v, s); }
ssize_t lgetxattr(const char *p, const char *n, void *v, size_t s) { return (ssize_t) rt_sys(SYS_lgetxattr, p, n, v, s); }
ssize_t fgetxattr(int fd, const char *n, void *v, size_t s) { return (ssize_t) rt_sys(SYS_fgetxattr, fd, n, v, s); }

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE: return (long) rt_auxv_pagesz;
    case _SC_CLK_TCK: return 100;
    case _SC_NPROCESSORS_ONLN: case _SC_NPROCESSORS_CONF: { unsigned long m[16]; long r = rt_raw(SYS_sched_getaffinity, 0, sizeof m, m, 0, 0, 0); if (r <= 0) return 1; long n = 0; for (long i = 0; i < r / 8; i++) n += __builtin_popcountl(m[i]); return n ? n : 1; }
    case _SC_OPEN_MAX: { struct rlimit l; if (getrlimit(RLIMIT_NOFILE, &l) == 0) return (long) l.rlim_cur; return 1024; }
    case _SC_PHYS_PAGES: { struct sysinfo s; if (sysinfo(&s) == 0) return (long) ((unsigned long) s.totalram * s.mem_unit / rt_auxv_pagesz); return -1; }
    case _SC_ARG_MAX: return 2097152;
    default: errno = EINVAL; return -1;
    }
}

/* ---- directories (the kernel's linux_dirent64 is the layout of struct dirent) */
struct rt_dir { int fd; size_t pos, len; char buf[4096] __attribute__((aligned(8))); };
DIR *fdopendir(int fd) { struct rt_dir *d = malloc(sizeof *d); if (!d) return 0; d->fd = fd; d->pos = d->len = 0; return (DIR *) d; }
DIR *opendir(const char *p) {
    long fd = rt_raw(SYS_openat, AT_FDCWD, p, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0, 0, 0);
    if (fd < 0) { errno = (int) -fd; return 0; }
    DIR *d = fdopendir((int) fd); if (!d) rt_raw(SYS_close, fd, 0, 0, 0, 0, 0); return d;
}
struct dirent *readdir(DIR *dp) {
    struct rt_dir *d = (struct rt_dir *) dp;
    if (d->pos >= d->len) { long r = rt_raw(SYS_getdents64, d->fd, d->buf, sizeof d->buf, 0, 0, 0); if (r <= 0) { if (r < 0) errno = (int) -r; return 0; } d->pos = 0; d->len = (size_t) r; }
    struct dirent *e = (struct dirent *) (d->buf + d->pos); d->pos += e->d_reclen; return e;
}
struct dirent64 *readdir64(DIR *d) { return (struct dirent64 *) readdir(d); }
int closedir(DIR *dp) { struct rt_dir *d = (struct rt_dir *) dp; int fd = d->fd; free(d); return (int) rt_sys(SYS_close, fd); }
int dirfd(DIR *dp) { return ((struct rt_dir *) dp)->fd; }
void rewinddir(DIR *dp) { struct rt_dir *d = (struct rt_dir *) dp; rt_raw(SYS_lseek, d->fd, 0, SEEK_SET, 0, 0, 0); d->pos = d->len = 0; }

/* ---- realpath: walk the components, following links (at most 40) */
char *realpath(const char *path, char *out) {
    char res[PATH_MAX], in[2 * PATH_MAX]; size_t rl = 1; int links = 0;
    if (!path) { errno = EINVAL; return 0; }
    if (!*path) { errno = ENOENT; return 0; }
    if (strlen(path) >= PATH_MAX) { errno = ENAMETOOLONG; return 0; }
    res[0] = '/'; res[1] = 0;
    if (path[0] != '/') { long r = rt_raw(SYS_getcwd, res, sizeof res, 0, 0, 0, 0); if (r < 0) { errno = (int) -r; return 0; } rl = strlen(res); }
    strcpy(in, path);
    char *p = in;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        char *q = p; while (*q && *q != '/') q++;
        size_t cl = (size_t) (q - p);
        if (cl == 1 && p[0] == '.') { p = q; continue; }
        if (cl == 2 && p[0] == '.' && p[1] == '.') { while (rl > 1 && res[rl - 1] != '/') rl--; if (rl > 1) rl--; while (rl > 1 && res[rl - 1] == '/') rl--; res[rl] = 0; if (!rl) { res[0] = '/'; rl = 1; res[1] = 0; } p = q; continue; }
        size_t old = rl;
        if (rl + cl + 2 > PATH_MAX) { errno = ENAMETOOLONG; return 0; }
        if (rl > 1) res[rl++] = '/';
        memcpy(res + rl, p, cl); rl += cl; res[rl] = 0;
        struct stat st; long r = rt_raw(SYS_newfstatat, AT_FDCWD, res, &st, AT_SYMLINK_NOFOLLOW, 0, 0);
        if (r < 0) { errno = (int) -r; return 0; }
        if (S_ISLNK(st.st_mode)) {
            if (++links > 40) { errno = ELOOP; return 0; }
            char lb[PATH_MAX]; long n = rt_raw(SYS_readlinkat, AT_FDCWD, res, lb, sizeof lb - 1, 0, 0);
            if (n < 0) { errno = (int) -n; return 0; }
            lb[n] = 0;
            size_t rest = strlen(q);
            if ((size_t) n + 1 + rest + 1 > sizeof in) { errno = ENAMETOOLONG; return 0; }
            memmove(in + n + 1, q, rest + 1); memcpy(in, lb, (size_t) n); in[n] = '/';
            p = in; rl = old; res[rl] = 0;
            if (lb[0] == '/') { rl = 1; res[1] = 0; }
            else if (rl == 0) { rl = 1; res[0] = '/'; res[1] = 0; }
            continue;
        }
        p = q;
    }
    if (rl > 1 && res[rl - 1] == '/') res[--rl] = 0;
    if (out) { strcpy(out, res); return out; }
    return strdup(res);
}

/* ---- exec */
int execve(const char *p, char *const a[], char *const e[]) { return (int) rt_sys(SYS_execve, p, a, e); }
int execv(const char *p, char *const a[]) { return execve(p, a, environ); }
int execvp(const char *file, char *const argv[]) {
    if (!*file) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) return execve(file, argv, environ);
    const char *path = getenv("PATH"); if (!path) path = "/usr/local/bin:/bin:/usr/bin";
    size_t fl = strlen(file); int eacces = 0;
    for (const char *p = path;;) {
        const char *q = strchr(p, ':'); size_t dl = q ? (size_t) (q - p) : strlen(p);
        char buf[PATH_MAX];
        if (dl + fl + 2 <= sizeof buf) {
            memcpy(buf, p, dl); if (dl) buf[dl++] = '/'; memcpy(buf + dl, file, fl + 1);
            execve(buf, argv, environ);
            if (errno == EACCES) eacces = 1; else if (errno != ENOENT && errno != ENOTDIR && errno != ESTALE && errno != ENODEV && errno != ETIMEDOUT) return -1;
        }
        if (!q) break;
        p = q + 1;
    }
    if (eacces) errno = EACCES;
    return -1;
}

/* ---- gmtime_r */
struct tm *gmtime_r(const time_t *tp, struct tm *tm) {
    long long t = *tp, days = t / 86400, rem = t % 86400; if (rem < 0) { rem += 86400; days--; }
    tm->tm_hour = (int) (rem / 3600); tm->tm_min = (int) (rem % 3600 / 60); tm->tm_sec = (int) (rem % 60);
    tm->tm_wday = (int) ((days + 4) % 7); if (tm->tm_wday < 0) tm->tm_wday += 7;
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned) (z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long long y = (long long) yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    tm->tm_year = (int) (y - 1900); tm->tm_mon = (int) m - 1; tm->tm_mday = (int) d;
    static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    tm->tm_yday = cum[m - 1] + (int) d - 1 + (leap && m > 2); tm->tm_isdst = 0; tm->tm_gmtoff = 0; tm->tm_zone = "GMT";
    return tm;
}
struct tm *localtime_r(const time_t *t, struct tm *tm) { return gmtime_r(t, tm); }
#endif /* PEMU_RT_SYS_H */

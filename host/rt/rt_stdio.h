/* host/rt/stdio.h - printf / scanf engines, FILE, DIR.  SPDX-License-Identifier: Apache-2.0 */
#ifndef PEMU_RT_STDIO_H
#define PEMU_RT_STDIO_H
#include <dirent.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ formatted output */
typedef struct { void (*put)(void *ctx, const char *s, size_t n); void *ctx; size_t total; } rt_sink;
static void rt_pad(rt_sink *k, char c, int n) { char b[32]; for (int i = 0; i < 32; i++) b[i] = c; while (n > 0) { int m = n > 32 ? 32 : n; k->put(k->ctx, b, (size_t) m); k->total += (size_t) m; n -= m; } }
static void rt_out(rt_sink *k, const char *s, size_t n) { if (n) { k->put(k->ctx, s, n); k->total += n; } }
static size_t rt_utoa(unsigned __int128 v, unsigned base, int upper, char *end) {         /* digits right-aligned before *end; returns count */
    const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef"; size_t n = 0;
    do { *--end = dg[(unsigned) (v % base)]; n++; v /= base; } while (v);
    return n;
}
static int rt_vformat(rt_sink *k, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { const char *s = f; while (f[1] && f[1] != '%') f++; rt_out(k, s, (size_t) (f - s + 1)); continue; }
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = -1, prec = -1, lng = 0; const char *spec = f;
        for (f++;; f++) { if (*f == '-') left = 1; else if (*f == '+') plus = 1; else if (*f == ' ') space = 1; else if (*f == '#') alt = 1; else if (*f == '0') zero = 1; else break; }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; } else if (*f >= '0' && *f <= '9') { width = 0; while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0'); }
        if (*f == '.') { f++; prec = 0; if (*f == '*') { prec = va_arg(ap, int); f++; } else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        for (;; f++) { if (*f == 'l') lng = lng == 1 ? 2 : 1; else if (*f == 'h') lng = lng == -1 ? -2 : -1; else if (*f == 'z' || *f == 'j' || *f == 't' || *f == 'q') lng = 2; else if (*f == 'L') lng = 3; else break; }
        char buf[160], *e = buf + sizeof buf; const char *body = 0; size_t bl = 0; char sign = 0; const char *pre = ""; int c = *f;
        switch (c) {
        case '%': rt_out(k, "%", 1); continue;
        case 'c': { buf[0] = (char) va_arg(ap, int); body = buf; bl = 1; zero = 0; break; }
        case 's': { body = va_arg(ap, const char *); if (!body) body = "(null)"; bl = prec >= 0 ? strnlen(body, (size_t) prec) : strlen(body); zero = 0; break; }
        case 'm': { body = strerror(errno); bl = strlen(body); zero = 0; break; }
        case 'p': { unsigned long v = (unsigned long) va_arg(ap, void *); if (!v) { body = "(nil)"; bl = 5; zero = 0; break; } bl = rt_utoa(v, 16, 0, e); body = e - bl; pre = "0x"; break; }
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            if (lng == -1) v = (short) v; else if (lng == -2) v = (signed char) v;
            unsigned long long u = v < 0 ? 0ULL - (unsigned long long) v : (unsigned long long) v; if (v < 0) sign = '-'; else if (plus) sign = '+'; else if (space) sign = ' ';
            bl = (prec == 0 && u == 0) ? 0 : rt_utoa(u, 10, 0, e); body = e - bl; break; }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long u = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            if (lng == -1) u = (unsigned short) u; else if (lng == -2) u = (unsigned char) u;
            unsigned b = c == 'u' ? 10 : c == 'o' ? 8 : 16;
            bl = (prec == 0 && u == 0) ? 0 : rt_utoa(u, b, c == 'X', e); body = e - bl;
            if (alt && c == 'o') { if (bl == 0 || *body != '0') { char *w = e - bl - 1; *w = '0'; body = w; bl++; } } else if (alt && u) { if (c == 'x') pre = "0x"; else if (c == 'X') pre = "0X"; }
            break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double d = lng == 3 ? (double) va_arg(ap, long double) : va_arg(ap, double);
            if (d != d) { body = "nan"; bl = 3; zero = 0; break; }
            if (__builtin_signbit(d)) { sign = '-'; d = -d; } else if (plus) sign = '+'; else if (space) sign = ' ';
            if (d > 1.7e308) { body = "inf"; bl = 3; zero = 0; break; }
            int p = prec < 0 ? 6 : prec > 40 ? 40 : prec;
            long double ld = d; unsigned __int128 ip = d >= 3.4e38 ? ~(unsigned __int128) 0 : (unsigned __int128) ld; long double fr = ld - (long double) ip;
            long double sc = 1; for (int i = 0; i < p; i++) sc *= 10;
            long double r = fr * sc; unsigned long long fi = (unsigned long long) r; long double dd = r - (long double) fi;
            if (dd > 0.5L || (dd == 0.5L && ((p ? fi : (unsigned long long) (ip & 1)) & 1))) fi++;                     /* nearest, ties to even like glibc */
            long double lim = sc; if ((long double) fi >= lim) { fi = 0; ip++; }
            char fb[64]; size_t fl = 0; if (p) { unsigned long long t = fi; for (int i = p - 1; i >= 0; i--) { fb[i] = (char) ('0' + t % 10); t /= 10; } fl = (size_t) p; }
            size_t il = rt_utoa(ip, 10, 0, e); char *ib = e - il;
            char tmp[160]; size_t o = 0; memcpy(tmp, ib, il); o = il;
            if (p || alt) tmp[o++] = '.';
            for (size_t i = 0; i < fl && o < sizeof tmp; i++) tmp[o++] = fb[i];
            memcpy(buf, tmp, o); body = buf; bl = o; break; }
        default: rt_out(k, spec, (size_t) (f - spec) + (*f ? 1 : 0)); if (!*f) f--; continue;
        }
        int zp = 0;
        if (c == 'd' || c == 'i' || c == 'u' || c == 'o' || c == 'x' || c == 'X') { if (prec >= 0) { zero = 0; if ((size_t) prec > bl) zp = prec - (int) bl; } }
        size_t pl = strlen(pre) + (sign ? 1 : 0);
        int total = (int) (pl + bl) + zp;
        if (zero && !left && width > total) zp += width - total, total = width;
        if (!left && width > total) rt_pad(k, ' ', width - total);
        if (sign) rt_out(k, &sign, 1);
        rt_out(k, pre, strlen(pre)); rt_pad(k, '0', zp); rt_out(k, body, bl);
        if (left && width > total) rt_pad(k, ' ', width - total);
    }
    return (int) k->total;
}
struct rt_sn { char *b; size_t cap, n; };
static void rt_sn_put(void *c, const char *s, size_t n) { struct rt_sn *o = c; for (size_t i = 0; i < n; i++, o->n++) if (o->n + 1 < o->cap) o->b[o->n] = s[i]; }
int vsnprintf(char *b, size_t cap, const char *f, va_list ap) {
    struct rt_sn o = { b, cap, 0 }; rt_sink k = { rt_sn_put, &o, 0 };
    int r = rt_vformat(&k, f, ap); if (cap) b[o.n < cap ? o.n : cap - 1] = 0; return r;
}
int snprintf(char *b, size_t cap, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, cap, f, ap); va_end(ap); return r; }
int vsprintf(char *b, const char *f, va_list ap) { return vsnprintf(b, (size_t) INT_MAX, f, ap); }
int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, (size_t) INT_MAX, f, ap); va_end(ap); return r; }
int vasprintf(char **out, const char *f, va_list ap) {
    va_list a2; va_copy(a2, ap); int n = vsnprintf(0, 0, f, a2); va_end(a2);
    if (n < 0) return -1;
    char *p = malloc((size_t) n + 1);
    if (!p) return -1;
    vsnprintf(p, (size_t) n + 1, f, ap); *out = p; return n;
}
int asprintf(char **out, const char *f, ...) { va_list ap; va_start(ap, f); int r = vasprintf(out, f, ap); va_end(ap); return r; }

/* ------------------------------------------------------------------ FILE */
struct rt_file { int fd, flags; unsigned char *buf; size_t cap, pos, len; struct rt_file *next, **prevp; };
enum { RTF_R = 1, RTF_W = 2, RTF_EOF = 4, RTF_ERR = 8, RTF_UNBUF = 16, RTF_LINE = 32, RTF_OWNBUF = 64, RTF_RDBUF = 128 };
static unsigned char rt_obuf[4096];
static struct rt_file rt_f_in = { 0, RTF_R, 0, 0, 0, 0, 0, 0 }, rt_f_out = { 1, RTF_W | RTF_LINE, rt_obuf, sizeof rt_obuf, 0, 0, 0, 0 }, rt_f_err = { 2, RTF_W | RTF_UNBUF, 0, 0, 0, 0, 0, 0 };
FILE *stdin = (FILE *) &rt_f_in, *stdout = (FILE *) &rt_f_out, *stderr = (FILE *) &rt_f_err;
static struct rt_file *rt_files; static volatile int rt_flock;
#define RTF(p) ((struct rt_file *) (p))
int rt_wr_all(int fd, const void *b, size_t n) {
    const char *p = b;
    while (n) { long r = rt_raw(SYS_write, fd, p, n, 0, 0, 0); if (r == -EINTR) continue; if (r < 0) { errno = (int) -r; return -1; } p += r; n -= (size_t) r; }
    return 0;
}
static int rt_fflush(struct rt_file *f) {
    if ((f->flags & RTF_W) && f->len) { int r = rt_wr_all(f->fd, f->buf, f->len); f->len = 0; if (r) { f->flags |= RTF_ERR; return EOF; } }
    return 0;
}
int fflush(FILE *s) {
    if (!s) { int r = 0; rt_lock(&rt_flock); for (struct rt_file *f = rt_files; f; f = f->next) r |= rt_fflush(f); r |= rt_fflush(&rt_f_out); rt_unlock(&rt_flock); return r; }
    return rt_fflush(RTF(s));
}
void rt_stdio_flush_all(void) { rt_fflush(&rt_f_out); rt_fflush(&rt_f_err); for (struct rt_file *f = rt_files; f; f = f->next) rt_fflush(f); }
static int rt_putn(struct rt_file *f, const char *s, size_t n) {
    if (!(f->flags & RTF_W)) { f->flags |= RTF_ERR; errno = EBADF; return -1; }
    if (f->flags & RTF_UNBUF || !f->buf) { if (rt_wr_all(f->fd, s, n)) { f->flags |= RTF_ERR; return -1; } return 0; }
    while (n) {
        size_t room = f->cap - f->len, m = n < room ? n : room; memcpy(f->buf + f->len, s, m); f->len += m; s += m; n -= m;
        if (f->len == f->cap && rt_fflush(f)) return -1;
    }
    return 0;
}
static void rt_fput(void *c, const char *s, size_t n) {
    struct rt_file *f = c; rt_putn(f, s, n);
    if ((f->flags & RTF_LINE) && memchr(s, '\n', n)) rt_fflush(f);
}
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { size_t t = sz * n; if (!t) return 0; rt_fput(s, p, t); return RTF(s)->flags & RTF_ERR ? 0 : n; }
int fputs(const char *p, FILE *s) { rt_fput(s, p, strlen(p)); return RTF(s)->flags & RTF_ERR ? EOF : 1; }
int fputc(int c, FILE *s) { char ch = (char) c; rt_fput(s, &ch, 1); return RTF(s)->flags & RTF_ERR ? EOF : (unsigned char) c; }
int putc(int c, FILE *s) { return fputc(c, s); }
int putchar(int c) { return fputc(c, stdout); }
int puts(const char *p) { rt_fput(stdout, p, strlen(p)); rt_fput(stdout, "\n", 1); return 1; }
int vfprintf(FILE *s, const char *f, va_list ap) { rt_sink k = { rt_fput, s, 0 }; return rt_vformat(&k, f, ap); }
int fprintf(FILE *s, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(s, f, ap); va_end(ap); return r; }
int vprintf(const char *f, va_list ap) { return vfprintf(stdout, f, ap); }
int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(stdout, f, ap); va_end(ap); return r; }
void perror(const char *m) { if (m && *m) { fputs(m, stderr); fputs(": ", stderr); } fputs(strerror(errno), stderr); fputc('\n', stderr); }

static int rt_fill(struct rt_file *f) {
    if (!f->buf) { f->buf = malloc(4096); if (!f->buf) return -1; f->cap = 4096; f->flags |= RTF_OWNBUF; }
    for (;;) {
        long r = rt_raw(SYS_read, f->fd, f->buf, f->cap, 0, 0, 0);
        if (r == -EINTR) continue;
        if (r < 0) { errno = (int) -r; f->flags |= RTF_ERR; return -1; }
        if (r == 0) { f->flags |= RTF_EOF; return -1; }
        f->pos = 0; f->len = (size_t) r; return 0;
    }
}
int fgetc(FILE *s) {
    struct rt_file *f = RTF(s);
    if (!(f->flags & RTF_R)) { f->flags |= RTF_ERR; return EOF; }
    if (!(f->flags & RTF_RDBUF)) { f->pos = f->len = 0; f->flags |= RTF_RDBUF; }
    if (f->pos >= f->len && rt_fill(f)) return EOF;
    return f->buf[f->pos++];
}
int getc(FILE *s) { return fgetc(s); }
int getchar(void) { return fgetc(stdin); }
size_t fread(void *p, size_t sz, size_t n, FILE *s) {
    size_t t = sz * n, o = 0; char *d = p;
    while (o < t) { int c = fgetc(s); if (c == EOF) break; d[o++] = (char) c; }
    return sz ? o / sz : 0;
}
char *fgets(char *b, int n, FILE *s) {
    int i = 0; if (n <= 0) return 0;
    while (i < n - 1) { int c = fgetc(s); if (c == EOF) break; b[i++] = (char) c; if (c == '\n') break; }
    if (!i) return 0;
    b[i] = 0; return b;
}
int feof(FILE *s) { return (RTF(s)->flags & RTF_EOF) != 0; }
int ferror(FILE *s) { return (RTF(s)->flags & RTF_ERR) != 0; }
void clearerr(FILE *s) { RTF(s)->flags &= ~(RTF_EOF | RTF_ERR); }
int fileno(FILE *s) { return RTF(s)->fd; }
int setvbuf(FILE *s, char *b, int mode, size_t sz) { (void) b; (void) sz; struct rt_file *f = RTF(s); f->flags &= ~(RTF_UNBUF | RTF_LINE); if (mode == _IONBF) { rt_fflush(f); f->flags |= RTF_UNBUF; } else if (mode == _IOLBF) f->flags |= RTF_LINE; return 0; }
void setbuf(FILE *s, char *b) { setvbuf(s, b, b ? _IOFBF : _IONBF, BUFSIZ); }
off_t lseek(int fd, off_t o, int w) { return (off_t) rt_sys(SYS_lseek, fd, o, w); }
int fseek(FILE *s, long off, int wh) {
    struct rt_file *f = RTF(s); rt_fflush(f);
    if (wh == SEEK_CUR && (f->flags & RTF_RDBUF)) off -= (long) (f->len - f->pos);
    f->pos = f->len = 0; f->flags &= ~(RTF_EOF | RTF_RDBUF);
    return lseek(f->fd, off, wh) < 0 ? -1 : 0;
}
long ftell(FILE *s) { struct rt_file *f = RTF(s); off_t o = lseek(f->fd, 0, SEEK_CUR); if (o < 0) return -1; return (long) o + (long) ((f->flags & RTF_W) ? f->len : 0) - (long) ((f->flags & RTF_RDBUF) ? f->len - f->pos : 0); }
void rewind(FILE *s) { fseek(s, 0, SEEK_SET); clearerr(s); }

static struct rt_file *rt_fnew(int fd, int fl) {
    struct rt_file *f = calloc(1, sizeof *f); if (!f) return 0;
    f->fd = fd; f->flags = fl; f->buf = 0;
    if (fl & RTF_W) { f->buf = malloc(4096); if (!f->buf) { free(f); return 0; } f->cap = 4096; f->flags |= RTF_OWNBUF; }
    rt_lock(&rt_flock); f->next = rt_files; f->prevp = &rt_files; if (rt_files) rt_files->prevp = &f->next; rt_files = f; rt_unlock(&rt_flock);
    return f;
}
static int rt_mode(const char *m, int *oflags) {
    int fl = 0, rw = 0, ex = 0;
    switch (*m++) { case 'r': fl = O_RDONLY; rw = RTF_R; break; case 'w': fl = O_WRONLY | O_CREAT | O_TRUNC; rw = RTF_W; break; case 'a': fl = O_WRONLY | O_CREAT | O_APPEND; rw = RTF_W; break; default: errno = EINVAL; return -1; }
    for (; *m; m++) { if (*m == '+') { fl = (fl & ~(O_RDONLY | O_WRONLY)) | O_RDWR; rw = RTF_R | RTF_W; } else if (*m == 'e') fl |= O_CLOEXEC; else if (*m == 'x') ex = 1; }
    if (ex) fl |= O_EXCL;
    *oflags = fl; return rw;
}
FILE *fopen(const char *path, const char *mode) {
    int fl, rw = rt_mode(mode, &fl); if (rw < 0) return 0;
    long fd = rt_raw(SYS_openat, AT_FDCWD, path, fl, 0666, 0, 0);
    if (fd < 0) { errno = (int) -fd; return 0; }
    struct rt_file *f = rt_fnew((int) fd, rw); if (!f) { rt_raw(SYS_close, fd, 0, 0, 0, 0, 0); return 0; }
    return (FILE *) f;
}
FILE *fdopen(int fd, const char *mode) { int fl; int rw = rt_mode(mode, &fl); if (rw < 0) return 0; return (FILE *) rt_fnew(fd, rw); }
int fclose(FILE *s) {
    struct rt_file *f = RTF(s); int r = rt_fflush(f);
    if (f == &rt_f_in || f == &rt_f_out || f == &rt_f_err) { rt_raw(SYS_close, f->fd, 0, 0, 0, 0, 0); return r; }
    rt_lock(&rt_flock); *f->prevp = f->next; if (f->next) f->next->prevp = f->prevp; rt_unlock(&rt_flock);
    if (rt_raw(SYS_close, f->fd, 0, 0, 0, 0, 0) < 0) r = EOF;
    if (f->flags & RTF_OWNBUF) free(f->buf);
    free(f); return r;
}
FILE *freopen(const char *path, const char *mode, FILE *s) {
    struct rt_file *f = RTF(s); int fl, rw = rt_mode(mode, &fl); if (rw < 0) return 0;
    rt_fflush(f);
    long fd = rt_raw(SYS_openat, AT_FDCWD, path, fl, 0666, 0, 0); if (fd < 0) { errno = (int) -fd; return 0; }
    rt_raw(SYS_dup3, fd, f->fd, 0, 0, 0, 0); rt_raw(SYS_close, fd, 0, 0, 0, 0, 0);
    f->flags = (f->flags & (RTF_OWNBUF | RTF_UNBUF | RTF_LINE)) | rw; f->pos = f->len = 0;
    if ((rw & RTF_W) && !f->buf) { f->buf = malloc(4096); f->cap = 4096; f->flags |= RTF_OWNBUF; }
    return s;
}
int isatty(int fd) { char t[64]; if (rt_raw(SYS_ioctl, fd, 0x5401 /* TCGETS */, t, 0, 0, 0) == 0) return 1; errno = ENOTTY; return 0; }

/* ------------------------------------------------------------------ sscanf */
int vsscanf(const char *s, const char *f, va_list ap) {
    int n = 0; const char *s0 = s;
    for (; *f; f++) {
        if (*f == ' ' || (*f >= 9 && *f <= 13)) { while (*s == ' ' || (*s >= 9 && *s <= 13)) s++; continue; }
        if (*f != '%') { if (*s != *f) return n ? n : (*s ? 0 : EOF); s++; continue; }
        f++; int sup = 0, w = 0, lng = 0; if (*f == '*') { sup = 1; f++; }
        while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
        for (;; f++) { if (*f == 'l') lng++; else if (*f == 'h') lng--; else if (*f == 'z' || *f == 'j' || *f == 't' || *f == 'q') lng = 2; else break; }
        int c = *f; if (c == '%') { if (*s != '%') return n; s++; continue; }
        if (c == 'n') { if (!sup) { int *p = va_arg(ap, int *); *p = (int) (s - s0); } continue; }
        if (c == 'c') { int m = w ? w : 1; for (int i = 0; i < m; i++) if (!s[i]) return n ? n : EOF; if (!sup) { char *p = va_arg(ap, char *); memcpy(p, s, (size_t) m); n++; } s += m; continue; }
        if (c != '[') while (*s == ' ' || (*s >= 9 && *s <= 13)) s++;
        if (!*s) return n ? n : EOF;
        if (c == 's') { const char *b = s; while (*s && !(*s == ' ' || (*s >= 9 && *s <= 13)) && (!w || s - b < w)) s++; if (s == b) return n; if (!sup) { char *p = va_arg(ap, char *); memcpy(p, b, (size_t) (s - b)); p[s - b] = 0; n++; } continue; }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o') {
            char tmp[80]; size_t l = 0; const char *b = s; (void) b;
            if (*s == '+' || *s == '-') tmp[l++] = *s++;
            int base = c == 'd' || c == 'u' ? 10 : c == 'i' ? 0 : c == 'o' ? 8 : 16;
            while (*s && (!w || (int) l < w) && l + 1 < sizeof tmp) {
                int ch = (unsigned char) *s, ok = (ch >= '0' && ch <= '9') || (base != 10 && base != 8 && ((ch | 32) >= 'a' && (ch | 32) <= 'f')) || ((ch == 'x' || ch == 'X') && (base == 0 || base == 16) && l && tmp[l - 1] == '0');
                if (!ok) break;
                tmp[l++] = *s++;
            }
            tmp[l] = 0; char *end; int neg, ovf; const char *q = tmp; unsigned long long v = rt_strtoull(q, &end, base, &neg, &ovf);
            if (end == tmp) return n;
            if (!sup) {
                long long sv = neg ? (long long) (0 - v) : (long long) v; unsigned long long uv = neg ? 0 - v : v;
                int sg = (c == 'd' || c == 'i');
                if (lng >= 1) { if (sg) *va_arg(ap, long long *) = sv; else *va_arg(ap, unsigned long long *) = uv; }
                else if (lng == -1) { if (sg) *va_arg(ap, short *) = (short) sv; else *va_arg(ap, unsigned short *) = (unsigned short) uv; }
                else if (lng <= -2) { if (sg) *va_arg(ap, signed char *) = (signed char) sv; else *va_arg(ap, unsigned char *) = (unsigned char) uv; }
                else { if (sg) *va_arg(ap, int *) = (int) sv; else *va_arg(ap, unsigned *) = (unsigned) uv; }
                n++;
            }
            continue;
        }
        return n;
    }
    return n;
}
int sscanf(const char *s, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsscanf(s, f, ap); va_end(ap); return r; }
int __isoc99_sscanf(const char *s, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsscanf(s, f, ap); va_end(ap); return r; }
#endif /* PEMU_RT_STDIO_H */

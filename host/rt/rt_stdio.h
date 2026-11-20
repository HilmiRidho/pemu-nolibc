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
/* ---- %f %e %g: the exact decimal expansion of the double (a base 1e9 big number: m * 2^e is m * 2^e, or m * 5^-e / 10^-e), rounded half-to-even at the digit asked for, like glibc.
 * Any precision works: a double has no non-zero digit further out than 1074 places, the rest is zeros.  long double arguments are converted to double. */
static void rt_big_mul(unsigned *L, int *nl, unsigned mu) {
    unsigned long long c = 0;
    for (int i = 0; i < *nl; i++) { unsigned long long t = (unsigned long long) L[i] * mu + c; L[i] = (unsigned) (t % 1000000000U); c = t / 1000000000U; }
    while (c) { L[(*nl)++] = (unsigned) (c % 1000000000U); c /= 1000000000U; }
}
/* d > 0, finite.  Digits (no leading zero, no trailing zero) go to dg; the value is 0.d1d2d3... * 10^pt.  Returns the digit count. */
static int rt_dbl_digits(double d, char *dg, int *ptp) {
    unsigned long long bits; memcpy(&bits, &d, 8);
    int ex = (int) ((bits >> 52) & 0x7ff), e; unsigned long long m = bits & ((1ULL << 52) - 1);
    if (ex) { m |= 1ULL << 52; e = ex - 1075; } else e = -1074;
    unsigned L[96]; int nl = 0, nd = 0, sh = e;                       /* 2^53 * 5^1074 < 2^2548: 86 limbs of 9 digits at most */
    while (m) { L[nl++] = (unsigned) (m % 1000000000U); m /= 1000000000U; }
    if (sh >= 0) { while (sh >= 29) { rt_big_mul(L, &nl, 1U << 29); sh -= 29; } if (sh) rt_big_mul(L, &nl, 1U << sh); }
    else { sh = -sh; while (sh >= 13) { rt_big_mul(L, &nl, 1220703125U); sh -= 13; } unsigned f5 = 1; while (sh-- > 0) f5 *= 5; if (f5 > 1) rt_big_mul(L, &nl, f5); }
    { unsigned t = L[nl - 1]; char tb[10]; int len = 0; do { tb[len++] = (char) ('0' + t % 10); t /= 10; } while (t); while (len) dg[nd++] = tb[--len]; }
    for (int i = nl - 2; i >= 0; i--) { unsigned v = L[i]; for (int j = 8; j >= 0; j--) { dg[nd + j] = (char) ('0' + v % 10); v /= 10; } nd += 9; }
    *ptp = e >= 0 ? nd : nd + e;
    while (nd > 1 && dg[nd - 1] == '0') nd--;
    return nd;
}
static int rt_dg(const char *dg, int nd, int i) { return i >= 0 && i < nd ? dg[i] - '0' : 0; }
/* keep n digits (n may be <= 0): round half to even on the exact value; may carry into a new leading digit (pt + 1) */
static void rt_round_digits(char *dg, int *nd, int *pt, int n) {
    if (n >= *nd) return;
    if (n < 0) { *nd = 0; return; }
    int r = dg[n] - '0', up = r > 5 || (r == 5 && (*nd > n + 1 || (n > 0 && ((dg[n - 1] - '0') & 1))));
    if (up) {
        if (n == 0) { dg[0] = '1'; *nd = 1; (*pt)++; return; }
        int i = n - 1; while (i >= 0 && dg[i] == '9') dg[i--] = '0';
        if (i < 0) { dg[0] = '1'; (*pt)++; } else dg[i]++;
    }
    *nd = n; while (*nd > 0 && dg[*nd - 1] == '0') (*nd)--;
}
static __attribute__((noinline)) void rt_fmt_float(rt_sink *k, double d, int c, int prec, int left, int plus, int space, int alt, int zero, int width) {
    char sign = 0, ob[1400]; int len = 0, extra = 0, tl = 0, up = c == 'F' || c == 'E' || c == 'G';
    if (__builtin_signbit(d)) { sign = '-'; d = -d; } else if (plus) sign = '+'; else if (space) sign = ' ';
    if (d != d || d > __DBL_MAX__) { memcpy(ob, d != d ? (up ? "NAN" : "nan") : (up ? "INF" : "inf"), 3); len = 3; zero = 0; }
    else {
        char dg[880]; int nd = 0, pt = 1, trim = 0, cl = c | 32;
        if (d != 0) nd = rt_dbl_digits(d, dg, &pt);
        if (prec > (1 << 30)) prec = 1 << 30;
        if (cl == 'g') {
            int P = prec < 0 ? 6 : prec == 0 ? 1 : prec;
            rt_round_digits(dg, &nd, &pt, P);
            int X = nd ? pt - 1 : 0;
            if (X < -4 || X >= P) { cl = 'e'; prec = P - 1; } else { cl = 'f'; prec = P - 1 - X; }
            trim = !alt;                                       /* %g drops trailing zeros (and a bare point) unless # */
        } else if (prec < 0) prec = 6;
        int p = prec;
        if (p > 1074) { if (!trim) extra = p - 1074; p = prec = 1074; }       /* beyond the last digit of a double: zeros, written without a buffer */
        if (cl == 'f') {
            rt_round_digits(dg, &nd, &pt, pt + prec);
            if (trim) { int last = 0; for (int j = 0; j < p; j++) if (rt_dg(dg, nd, pt + j)) last = j + 1; p = last; }
            if (pt <= 0) ob[len++] = '0'; else for (int i = 0; i < pt; i++) ob[len++] = (char) ('0' + rt_dg(dg, nd, i));
            if (p > 0 || alt) ob[len++] = '.';
            for (int j = 0; j < p; j++) ob[len++] = (char) ('0' + rt_dg(dg, nd, pt + j));
        } else {
            rt_round_digits(dg, &nd, &pt, prec + 1);
            int X = nd ? pt - 1 : 0, ax = X < 0 ? -X : X;
            if (trim) { int last = 0; for (int j = 1; j <= p; j++) if (rt_dg(dg, nd, j)) last = j; p = last; }
            ob[len++] = (char) ('0' + rt_dg(dg, nd, 0));
            if (p > 0 || alt) ob[len++] = '.';
            for (int j = 1; j <= p; j++) ob[len++] = (char) ('0' + rt_dg(dg, nd, j));
            int e0 = len; ob[len++] = up ? 'E' : 'e'; ob[len++] = X < 0 ? '-' : '+';
            if (ax >= 100) ob[len++] = (char) ('0' + ax / 100);
            ob[len++] = (char) ('0' + ax / 10 % 10); ob[len++] = (char) ('0' + ax % 10); tl = len - e0;
        }
    }
    int zp = 0, total = len + extra + (sign ? 1 : 0);
    if (zero && !left && width > total) { zp = width - total; total = width; }
    if (!left && width > total) rt_pad(k, ' ', width - total);
    if (sign) rt_out(k, &sign, 1);
    rt_pad(k, '0', zp); rt_out(k, ob, (size_t) (len - tl)); rt_pad(k, '0', extra); rt_out(k, ob + len - tl, (size_t) tl);
    if (left && width > total) rt_pad(k, ' ', width - total);
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
            rt_fmt_float(k, d, c, prec, left, plus, space, alt, zero, width); continue; }
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
    if ((f->flags & RTF_W) && !(f->flags & RTF_RDBUF) && f->len) { int r = rt_wr_all(f->fd, f->buf, f->len); f->len = 0; if (r) { f->flags |= RTF_ERR; return EOF; } }
    return 0;
}
/* a stream open for reading AND writing ("r+", "w+", "a+") has one buffer: while RTF_RDBUF is set it holds data READ ahead, not data to write.  Leaving read mode gives the
 * unread bytes back to the file offset (lseek); force: drop them even when the file is not seekable (a socket, a pipe). */
static void rt_rdsync(struct rt_file *f, int force) {
    if (!(f->flags & RTF_RDBUF)) return;
    if (f->pos < f->len && rt_raw(SYS_lseek, f->fd, -(long) (f->len - f->pos), SEEK_CUR, 0, 0, 0) < 0 && !force) return;
    f->pos = f->len = 0; f->flags &= ~RTF_RDBUF;
}
int fflush(FILE *s) {
    if (!s) { int r = 0; rt_lock(&rt_flock); for (struct rt_file *f = rt_files; f; f = f->next) r |= rt_fflush(f); r |= rt_fflush(&rt_f_out); rt_unlock(&rt_flock); return r; }
    struct rt_file *f = RTF(s); int r = rt_fflush(f); rt_rdsync(f, 0); return r;
}
void rt_stdio_flush_all(void) { rt_fflush(&rt_f_out); rt_fflush(&rt_f_err); for (struct rt_file *f = rt_files; f; f = f->next) rt_fflush(f); }
static int rt_putn(struct rt_file *f, const char *s, size_t n) {
    if (!(f->flags & RTF_W)) { f->flags |= RTF_ERR; errno = EBADF; return -1; }
    rt_rdsync(f, 1);                                        /* read mode -> write mode: the buffer is not ours to write out */
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
    if (!(f->flags & RTF_RDBUF)) { if (f->flags & RTF_W) rt_fflush(f); f->pos = f->len = 0; f->flags |= RTF_RDBUF; }      /* write mode -> read mode: write out first */
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
long ftell(FILE *s) { struct rt_file *f = RTF(s); off_t o = lseek(f->fd, 0, SEEK_CUR); if (o < 0) return -1; return (long) o + (long) (((f->flags & RTF_W) && !(f->flags & RTF_RDBUF)) ? f->len : 0) - (long) ((f->flags & RTF_RDBUF) ? f->len - f->pos : 0); }
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
    struct rt_file *f = RTF(s); int r = rt_fflush(f); rt_rdsync(f, 0);
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
/* ---- the text of a floating point number as the correctly rounded double (round half to even, like glibc's strtod): exact big number arithmetic, no long double.
 * The digits (up to RT_SD of them, then a "sticky" 1 when the rest is not all zeros) are a big integer N; the value is N * 10^e10.
 *   e10 >= 0: N * 10^e10 is an integer; its top 56 bits and a sticky bit are rounded.
 *   e10 <  0: q = floor(N * 2^s / 10^-e10) is computed bit by bit with s chosen so that 2^54 <= q < 2^56; the remainder is the sticky bit.
 * A fast path handles up to 15 digits and |e10| <= 22 with one exact double operation (Clinger). */
#define RT_SD 800                                                /* digits kept (a double's halfway point has at most 767), then one sticky digit */
/* little endian 32-bit limbs; every operation takes the limb count nl, chosen from the input (3 limbs for 17 digits, 123 at most: 10^1125 * 2^56 fits) */
static void rt_bn_mul(unsigned *a, int nl, unsigned m, unsigned add) {
    unsigned long long c = add;
    for (int i = 0; i < nl; i++) { unsigned long long t = (unsigned long long) a[i] * m + c; a[i] = (unsigned) t; c = t >> 32; }
}
static int rt_bn_bits(const unsigned *a, int nl) {
    int i = nl - 1; while (i >= 0 && !a[i]) i--;
    return i < 0 ? 0 : i * 32 + 32 - __builtin_clz(a[i]);
}
static void rt_bn_shl(unsigned *a, int nl, int s) {
    int ws = s / 32, bs = s % 32;
    for (int i = nl - 1; i >= 0; i--) { unsigned v = i - ws >= 0 ? a[i - ws] << bs : 0; if (bs && i - ws - 1 >= 0) v |= a[i - ws - 1] >> (32 - bs); a[i] = v; }
}
static void rt_bn_shr1(unsigned *a, int nl) { for (int i = 0; i < nl; i++) a[i] = (a[i] >> 1) | (i + 1 < nl ? a[i + 1] << 31 : 0); }
static int rt_bn_cmp(const unsigned *a, const unsigned *b, int nl) {
    for (int i = nl - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static void rt_bn_sub(unsigned *a, const unsigned *b, int nl) {
    unsigned long long br = 0;
    for (int i = 0; i < nl; i++) { unsigned long long t = (unsigned long long) a[i] - b[i] - br; a[i] = (unsigned) t; br = (t >> 32) & 1; }
}
static void rt_bn_pow10(unsigned *a, int nl, int k) {              /* a *= 10^k */
    static const unsigned p[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };
    for (; k >= 9; k -= 9) rt_bn_mul(a, nl, 1000000000U, 0);
    if (k) rt_bn_mul(a, nl, p[k], 0);
}
static double rt_p2(int k) { unsigned long long b = (unsigned long long) (k + 1023) << 52; double d; memcpy(&d, &b, 8); return d; }
/* q * 2^e2 (q != 0, 64 bits at most, sticky: more non-zero bits below q) rounded to a double, subnormals and overflow included */
static double rt_round_pack(unsigned long long q, int e2, int sticky) {
    int bl = 64 - __builtin_clzll(q), top = bl - 1 + e2;
    if (top > 1023) return __builtin_inf();
    int keep = top >= -1022 ? 53 : 53 - (-1022 - top), drop = bl - keep;
    if (keep < 0) return 0.0;
    unsigned long long kept = q;
    if (drop > 0) {
        unsigned long long rem = drop >= 64 ? q : q & ((1ULL << drop) - 1), half = 1ULL << (drop - 1);
        kept = drop >= 64 ? 0 : q >> drop; e2 += drop;
        if (rem > half || (rem == half && (sticky || (kept & 1)))) kept++;
    }
    double v = (double) kept;
    while (e2 > 1000) { v *= rt_p2(1000); e2 -= 1000; }
    while (e2 < -1000) { v *= rt_p2(-1000); e2 += 1000; }
    return v * rt_p2(e2);
}
/* digits dg[0..nd) (no leading zero), value dg * 10^e10 (nd + e10 is the decimal point position), sticky: non-zero digits were dropped after them */
static double rt_dec_to_dbl(const char *dg, int nd, int e10, int sticky) {
    while (nd > 0 && dg[nd - 1] == '0') { nd--; e10++; }
    if (nd == 0) return 0.0;
    if (nd + e10 > 310) return __builtin_inf();
    if (nd + e10 < -324) return 0.0;
    if (!sticky && nd <= 15 && e10 >= -22 && e10 <= 22) {
        unsigned long long m = 0; for (int i = 0; i < nd; i++) m = m * 10 + (unsigned) (dg[i] - '0');
        double p = 1; for (int i = e10 < 0 ? -e10 : e10; i > 0; i--) p *= 10;
        return e10 < 0 ? (double) m / p : (double) m * p;
    }
    /* the biggest number held: N * 10^e10 (e10 >= 0) or N * 2^s / 10^-e10 (about max(nd, -e10) decimal digits of bits + 56), 10 / 3 bits per digit rounds up */
    int nl = ((e10 >= 0 ? nd + e10 : nd > -e10 ? nd : -e10) * 10 / 3 + 64) / 32 + 2;
    unsigned N[nl], D[nl], T[nl]; memset(N, 0, sizeof N);
    for (int i = 0; i < nd;) {                                         /* 9 digits at a time */
        int m = nd - i < 9 ? nd - i : 9; unsigned v = 0, pw = 1; for (int j = 0; j < m; j++, pw *= 10) v = v * 10 + (unsigned) (dg[i + j] - '0');
        rt_bn_mul(N, nl, pw, v); i += m;
    }
    if (e10 >= 0) {
        rt_bn_pow10(N, nl, e10);
        int bl = rt_bn_bits(N, nl), sh = bl > 56 ? bl - 56 : 0, st = 0; unsigned long long q = 0;
        for (int i = 0; i < sh / 32; i++) if (N[i]) st = 1;
        if (sh % 32 && (N[sh / 32] & ((1U << (sh % 32)) - 1))) st = 1;
        for (int i = bl - 1; i >= sh; i--) q = q << 1 | (N[i / 32] >> (i % 32) & 1);
        return rt_round_pack(q, sh, st);
    }
    memset(D, 0, sizeof D); D[0] = 1; rt_bn_pow10(D, nl, -e10);
    int s = rt_bn_bits(D, nl) - rt_bn_bits(N, nl) + 55; unsigned long long q = 0;
    if (s >= 0) rt_bn_shl(N, nl, s); else rt_bn_shl(D, nl, -s);
    memcpy(T, D, sizeof T); rt_bn_shl(T, nl, 55);
    for (int i = 55; i >= 0; i--) { if (rt_bn_cmp(N, T, nl) >= 0) { rt_bn_sub(N, T, nl); q |= 1ULL << i; } rt_bn_shr1(T, nl); }
    for (int i = 0; i < nl; i++) if (N[i]) { sticky = 1; break; }
    return rt_round_pack(q, -s, sticky);
}
/* sscanf %f: [sign] digits [. digits] [e [sign] digits] | inf | infinity | nan, at most w characters (0: no limit).  Returns 0 and leaves *sp when nothing could be read. */
static int rt_scan_float(const char **sp, int w, double *out) {
    const char *s = *sp; int l = 0, nds = 0, neg = 0, nd = 0, sticky = 0, dexp = 0; char dg[RT_SD + 1];
#define RT_SC(cond) ((!w || l < w) && *s && (cond))
    if (RT_SC(*s == '+' || *s == '-')) { neg = *s == '-'; s++; l++; }
    if (RT_SC(s[0] == '0' && (s[1] | 32) == 'x') && (!w || l + 2 < w)) {
        unsigned long long q = 0; int e2 = 0, hs = 0, any = 0;               /* 0x hex digits [. hex digits] [p [sign] decimal exponent of two]: exact, 60 bits then sticky */
        s += 2; l += 2;
        for (int fr = 0; ; fr = 1) {
            while (RT_SC(isxdigit((unsigned char) *s))) {
                int c = (unsigned char) *s++, v = c <= '9' ? c - '0' : (c | 32) - 'a' + 10; l++; any = 1;
                if (!q && !v) { if (fr) e2 -= 4; }
                else if (q < (1ULL << 60)) { q = q << 4 | (unsigned) v; if (fr) e2 -= 4; }
                else { if (v) hs = 1; if (!fr) e2 += 4; }
            }
            if (fr || !RT_SC(*s == '.')) break;
            s++; l++;
        }
        if (!any) return 0;                                                   /* "0x" and no digit: a matching failure, like glibc (the longest prefix of a hex float) */
        if (RT_SC((*s | 32) == 'p')) {
            const char *s0 = s; int l0 = l, ed = 0, en = 0, ev = 0; s++; l++;
            if (RT_SC(*s == '+' || *s == '-')) { en = *s == '-'; s++; l++; }
            while (RT_SC(*s >= '0' && *s <= '9')) { if (ev < 100000) ev = ev * 10 + (*s - '0'); s++; l++; ed++; }
            if (!ed) { s = s0; l = l0; } else e2 += en ? -ev : ev;
        }
        double v = q ? rt_round_pack(q, e2, hs) : 0.0;
        *out = neg ? -v : v; *sp = s; return 1;
    }
    for (int fr = 0; ; fr = 1) {
        while (RT_SC(*s >= '0' && *s <= '9')) {
            int c = *s++; l++; nds++;
            if (!nd && c == '0') { if (fr) dexp--; }
            else if (nd < RT_SD) { dg[nd++] = (char) c; if (fr) dexp--; }
            else { if (c != '0') sticky = 1; if (!fr) dexp++; }
        }
        if (fr || !RT_SC(*s == '.')) break;
        s++; l++;
    }
    if (nds) {
        if (RT_SC((*s | 32) == 'e')) {
            const char *s0 = s; int l0 = l, ed = 0, en = 0, ev = 0; s++; l++;
            if (RT_SC(*s == '+' || *s == '-')) { en = *s == '-'; s++; l++; }
            while (RT_SC(*s >= '0' && *s <= '9')) { if (ev < 100000) ev = ev * 10 + (*s - '0'); s++; l++; ed++; }
            if (!ed) { s = s0; l = l0; } else dexp += en ? -ev : ev;       /* "1e" or "1e+": the number is the "1" */
        }
        if (sticky) { dg[nd++] = '1'; dexp--; }
        double v = rt_dec_to_dbl(dg, nd, dexp, 0);
        *out = neg ? -v : v;
    }
    else if (!strncasecmp(s, "inf", 3)) { s += 3; if (!strncasecmp(s, "inity", 5)) s += 5; *out = neg ? -__builtin_inf() : __builtin_inf(); }
    else if (!strncasecmp(s, "nan", 3)) { s += 3; *out = __builtin_nan(""); }
    else return 0;
#undef RT_SC
    *sp = s; return 1;
}
int vsscanf(const char *s, const char *f, va_list ap) {
    int n = 0; const char *s0 = s;
    for (; *f; f++) {
        if (*f == ' ' || (*f >= 9 && *f <= 13)) { while (*s == ' ' || (*s >= 9 && *s <= 13)) s++; continue; }
        if (*f != '%') { if (*s != *f) return n ? n : (*s ? 0 : EOF); s++; continue; }
        f++; int sup = 0, w = 0, lng = 0; if (*f == '*') { sup = 1; f++; }
        while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
        for (;; f++) { if (*f == 'l') lng++; else if (*f == 'h') lng--; else if (*f == 'z' || *f == 'j' || *f == 't' || *f == 'q') lng = 2; else if (*f == 'L') lng = 3; else break; }
        int c = *f; if (c == 'p') { c = 'x'; lng = 2; }
        if (c == '%') { if (*s != '%') return n; s++; continue; }
        if (c == 'n') { if (!sup) { int *p = va_arg(ap, int *); *p = (int) (s - s0); } continue; }
        if (c == 'c') { int m = w ? w : 1; for (int i = 0; i < m; i++) if (!s[i]) return n ? n : EOF; if (!sup) { char *p = va_arg(ap, char *); memcpy(p, s, (size_t) m); n++; } s += m; continue; }
        if (c != '[') while (*s == ' ' || (*s >= 9 && *s <= 13)) s++;
        if (!*s) return n ? n : EOF;
        if (c == 's') { const char *b = s; while (*s && !(*s == ' ' || (*s >= 9 && *s <= 13)) && (!w || s - b < w)) s++; if (s == b) return n; if (!sup) { char *p = va_arg(ap, char *); memcpy(p, b, (size_t) (s - b)); p[s - b] = 0; n++; } continue; }
        if (c == '[') {
            const char *set = f + 1, *se; int ng = 0; if (*set == '^') { ng = 1; set++; }
            se = set; if (*se == ']') se++;                                       /* a leading ] is a member */
            while (*se && *se != ']') se++;
            if (!*se) return n;
            const char *b = s;
            while (*s && (!w || s - b < w)) {
                int in = 0;
                for (const char *q = set; q < se; q++) {
                    if (q + 2 < se && q[1] == '-') { if ((unsigned char) *s >= (unsigned char) q[0] && (unsigned char) *s <= (unsigned char) q[2]) in = 1; q += 2; }
                    else if (*q == *s) in = 1;
                }
                if (in == ng) break;
                s++;
            }
            if (s == b) return n;
            if (!sup) { char *p = va_arg(ap, char *); memcpy(p, b, (size_t) (s - b)); p[s - b] = 0; n++; }
            f = se; continue;
        }
        if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G' || c == 'a' || c == 'A') {
            double v; if (!rt_scan_float(&s, w, &v)) return n;
            if (!sup) { if (lng == 3) *va_arg(ap, long double *) = (long double) v; else if (lng >= 1) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float) v; n++; }
            continue;
        }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o') {
            char tmp[80]; size_t l = 0;
            int base = c == 'd' || c == 'u' ? 10 : c == 'i' ? 0 : c == 'o' ? 8 : 16;
            if (*s == '+' || *s == '-') tmp[l++] = *s++;
            if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x' && isxdigit((unsigned char) s[2]) && (!w || (int) l + 2 < w)) { tmp[l++] = *s++; tmp[l++] = *s++; base = 16; }
            else if (base == 0) base = s[0] == '0' ? 8 : 10;
            while (*s && (!w || (int) l < w) && l + 1 < sizeof tmp) {
                int ch = (unsigned char) *s, dv = ch >= '0' && ch <= '9' ? ch - '0' : (ch | 32) >= 'a' && (ch | 32) <= 'z' ? (ch | 32) - 'a' + 10 : 99;
                if (dv >= base) break;                                            /* only digits of this base are consumed */
                tmp[l++] = *s++;
            }
            tmp[l] = 0; char *end; int neg, ovf; unsigned long long v = rt_strtoull(tmp, &end, base, &neg, &ovf);
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

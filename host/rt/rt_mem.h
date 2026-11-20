/* host/rt/mem.h - mem*, str*, strtol, strerror, environment, malloc.  SPDX-License-Identifier: Apache-2.0 */
#ifndef PEMU_RT_MEM_H
#define PEMU_RT_MEM_H
#include <ctype.h>
#include <limits.h>

/* the compiler must not turn these loops back into calls to themselves.  gcc: the optimize attribute.  clang ignores that attribute (warning "unknown attribute
 * 'optimize'") but its loop-idiom pass does the same rewrite (a copy / fill / strlen loop becomes a call to memcpy / memset / strlen, here inside memcpy / memset /
 * strlen themselves: endless recursion, SIGSEGV at the first call; the Android NDK build is clang).  no_builtin without arguments: no library call is recognised
 * or generated in this function. */
#if defined(__clang__)
#if __has_attribute(no_builtin)
#define RT_NOPAT __attribute__((no_builtin))
#else
#define RT_NOPAT __attribute__((optnone))
#endif
#else
#define RT_NOPAT __attribute__((optimize("no-tree-loop-distribute-patterns")))
#endif

typedef unsigned long long __attribute__((may_alias, aligned(1))) rt_u64u;
typedef unsigned int __attribute__((may_alias, aligned(1))) rt_u32u;
typedef unsigned short __attribute__((may_alias, aligned(1))) rt_u16u;
RT_NOPAT void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    if (n <= 16) {                                         /* small copies: a few loads and stores, no rep prefix start-up cost */
        if (n >= 8) { rt_u64u x = *(const rt_u64u *) b, y = *(const rt_u64u *) (b + n - 8); *(rt_u64u *) a = x; *(rt_u64u *) (a + n - 8) = y; }
        else if (n >= 4) { rt_u32u x = *(const rt_u32u *) b, y = *(const rt_u32u *) (b + n - 4); *(rt_u32u *) a = x; *(rt_u32u *) (a + n - 4) = y; }
        else if (n >= 2) { rt_u16u x = *(const rt_u16u *) b, y = *(const rt_u16u *) (b + n - 2); *(rt_u16u *) a = x; *(rt_u16u *) (a + n - 2) = y; }
        else if (n) *a = *b;
        return d;
    }
    if (n <= 32) { rt_u64u w0 = *(const rt_u64u *) b, w1 = *(const rt_u64u *) (b + 8), w2 = *(const rt_u64u *) (b + n - 16), w3 = *(const rt_u64u *) (b + n - 8);
        *(rt_u64u *) a = w0; *(rt_u64u *) (a + 8) = w1; *(rt_u64u *) (a + n - 16) = w2; *(rt_u64u *) (a + n - 8) = w3; return d; }
#if defined(__x86_64__)
    void *r = d; __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory"); return r;
#else
    while (n--) *a++ = *b++; return d;
#endif
}
RT_NOPAT void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    if (a == b || !n) return d;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    while (n--) a[n] = b[n];
    return d;
}
RT_NOPAT void *memset(void *d, int c, size_t n) {
    unsigned char *a = d; unsigned long long v = (unsigned char) c * 0x0101010101010101ULL;
    if (n <= 16) {
        if (n >= 8) { *(rt_u64u *) a = v; *(rt_u64u *) (a + n - 8) = v; }
        else if (n >= 4) { *(rt_u32u *) a = (unsigned) v; *(rt_u32u *) (a + n - 4) = (unsigned) v; }
        else if (n >= 2) { *(rt_u16u *) a = (unsigned short) v; *(rt_u16u *) (a + n - 2) = (unsigned short) v; }
        else if (n) *a = (unsigned char) c;
        return d;
    }
    if (n <= 32) { *(rt_u64u *) a = v; *(rt_u64u *) (a + 8) = v; *(rt_u64u *) (a + n - 16) = v; *(rt_u64u *) (a + n - 8) = v; return d; }
#if defined(__x86_64__)
    void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r;
#else
    while (n--) *a++ = (unsigned char) c; return d;
#endif
}
int bcmp(const void *x, const void *y, size_t n) { return memcmp(x, y, n); }      /* clang turns some memcmp == 0 into bcmp */
RT_NOPAT int memcmp(const void *x, const void *y, size_t n) {
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
RT_NOPAT void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s; for (; n; n--, p++) if (*p == (unsigned char) c) return (void *) p;
    return 0;
}
RT_NOPAT size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t) (p - s); }
RT_NOPAT size_t strnlen(const char *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
RT_NOPAT int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (unsigned char) *a - (unsigned char) *b; }
RT_NOPAT int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { if (*a != *b) return (unsigned char) *a - (unsigned char) *b; if (!*a) return 0; }
    return 0;
}
RT_NOPAT char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
RT_NOPAT char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
RT_NOPAT char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
RT_NOPAT char *strncat(char *d, const char *s, size_t n) { char *e = d + strlen(d); size_t i = 0; for (; i < n && s[i]; i++) e[i] = s[i]; e[i] = 0; return d; }
RT_NOPAT char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char) c) return (char *) s; if (!*s) return 0; } }
RT_NOPAT char *strrchr(const char *s, int c) { const char *r = 0; for (;; s++) { if (*s == (char) c) r = s; if (!*s) return (char *) r; } }
RT_NOPAT char *strstr(const char *h, const char *n) {
    size_t l = strlen(n); if (!l) return (char *) h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *) h;
    return 0;
}
RT_NOPAT size_t strspn(const char *s, const char *a) { size_t i = 0; while (s[i] && strchr(a, s[i])) i++; return i; }
RT_NOPAT size_t strcspn(const char *s, const char *r) { size_t i = 0; while (s[i] && !strchr(r, s[i])) i++; return i; }
RT_NOPAT char *strpbrk(const char *s, const char *a) { s += strcspn(s, a); return *s ? (char *) s : 0; }
RT_NOPAT char *strtok_r(char *s, const char *d, char **sp) {
    if (!s) s = *sp;
    if (!s) return 0;
    s += strspn(s, d);
    if (!*s) { *sp = 0; return 0; }
    char *e = s + strcspn(s, d);
    if (*e) *e++ = 0; else e = 0;
    *sp = e; return s;
}
static char *rt_strtok_save; char *strtok(char *s, const char *d) { return strtok_r(s, d, &rt_strtok_save); }
int strcasecmp(const char *a, const char *b) { while (*a && tolower((unsigned char) *a) == tolower((unsigned char) *b)) a++, b++; return tolower((unsigned char) *a) - tolower((unsigned char) *b); }
int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { int d = tolower((unsigned char) *a) - tolower((unsigned char) *b); if (d) return d; if (!*a) return 0; }
    return 0;
}
/* ctype goes through glibc's tables (__ctype_b_loc ...): own, C locale only */
static const unsigned short *rt_ctype_tab(void) {
    static unsigned short t[384]; static int init;
    if (!init) {
        for (int c = -128; c < 256; c++) {
            unsigned short m = 0; int u = c & 0xff;
            if (c >= 0 && c < 128) {
                if (u >= 'A' && u <= 'Z') m |= _ISupper | _ISalpha | _ISalnum;
                if (u >= 'a' && u <= 'z') m |= _ISlower | _ISalpha | _ISalnum;
                if (u >= '0' && u <= '9') m |= _ISdigit | _ISalnum;
                if ((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')) m |= _ISxdigit;
                if (u == ' ' || (u >= 9 && u <= 13)) m |= _ISspace;
                if (u == ' ' || u == '\t') m |= _ISblank;
                if (u < 32 || u == 127) m |= _IScntrl;
                if (u >= 32 && u < 127) m |= _ISprint;
                if (u > 32 && u < 127) m |= _ISgraph;
                if (u > 32 && u < 127 && !(m & _ISalnum)) m |= _ISpunct;
            }
            t[c + 128] = m;
        }
        init = 1;
    }
    return t + 128;
}
const unsigned short **__ctype_b_loc(void) { static const unsigned short *p; p = rt_ctype_tab(); return &p; }
const int **__ctype_tolower_loc(void) { static int t[384]; static const int *p; if (!p) { for (int c = -128; c < 256; c++) t[c + 128] = (c >= 'A' && c <= 'Z') ? c + 32 : c; p = t + 128; } return &p; }
const int **__ctype_toupper_loc(void) { static int t[384]; static const int *p; if (!p) { for (int c = -128; c < 256; c++) t[c + 128] = (c >= 'a' && c <= 'z') ? c - 32 : c; p = t + 128; } return &p; }
int (tolower)(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int (toupper)(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* ---- numbers */
static unsigned long long rt_strtoull(const char *s, char **end, int base, int *neg, int *ovf) {
    const char *p = s; unsigned long long v = 0; *neg = 0; *ovf = 0;
    while (*p == ' ' || (*p >= 9 && *p <= 13)) p++;
    if (*p == '-') { *neg = 1; p++; } else if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char) p[2])) { p += 2; base = 16; }
    else if (base == 0) base = p[0] == '0' ? 8 : 10;
    const char *d0 = p;
    for (;; p++) {
        int c = (unsigned char) *p, dg;
        if (c >= '0' && c <= '9') dg = c - '0'; else if (c >= 'a' && c <= 'z') dg = c - 'a' + 10; else if (c >= 'A' && c <= 'Z') dg = c - 'A' + 10; else break;
        if (dg >= base) break;
        if (v > (~0ULL - (unsigned) dg) / (unsigned) base) *ovf = 1; else v = v * (unsigned) base + (unsigned) dg;
    }
    if (p == d0) { if (end) *end = (char *) s; return 0; }
    if (end) *end = (char *) p;
    return v;
}
unsigned long long strtoull(const char *s, char **e, int b) { int n, o; unsigned long long v = rt_strtoull(s, e, b, &n, &o); if (o) { errno = ERANGE; return ~0ULL; } return n ? -v : v; }
unsigned long strtoul(const char *s, char **e, int b) { return (unsigned long) strtoull(s, e, b); }
long long strtoll(const char *s, char **e, int b) {
    int n, o; unsigned long long v = rt_strtoull(s, e, b, &n, &o);
    if (o || (!n && v > (unsigned long long) LLONG_MAX) || (n && v > (unsigned long long) LLONG_MAX + 1)) { errno = ERANGE; return n ? LLONG_MIN : LLONG_MAX; }
    return n ? (long long) (0 - v) : (long long) v;
}
long strtol(const char *s, char **e, int b) { return (long) strtoll(s, e, b); }
int atoi(const char *s) { return (int) strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
long long atoll(const char *s) { return strtoll(s, 0, 10); }
/* glibc 2.38+ headers redirect strtol & co to these names */
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {     /* heap sort: no allocation, no recursion */
    char *b = base, t[256]; if (sz > sizeof t || n < 2) return;
#define RT_AT(i) (b + (i) * sz)
    for (size_t i = n / 2; i-- > 0;) {
        size_t r = i;
        for (;;) { size_t c = 2 * r + 1; if (c >= n) break; if (c + 1 < n && cmp(RT_AT(c), RT_AT(c + 1)) < 0) c++; if (cmp(RT_AT(r), RT_AT(c)) >= 0) break; memcpy(t, RT_AT(r), sz); memcpy(RT_AT(r), RT_AT(c), sz); memcpy(RT_AT(c), t, sz); r = c; }
    }
    for (size_t e = n; e-- > 1;) {
        memcpy(t, RT_AT(0), sz); memcpy(RT_AT(0), RT_AT(e), sz); memcpy(RT_AT(e), t, sz);
        size_t r = 0;
        for (;;) { size_t c = 2 * r + 1; if (c >= e) break; if (c + 1 < e && cmp(RT_AT(c), RT_AT(c + 1)) < 0) c++; if (cmp(RT_AT(r), RT_AT(c)) >= 0) break; memcpy(t, RT_AT(r), sz); memcpy(RT_AT(r), RT_AT(c), sz); memcpy(RT_AT(c), t, sz); r = c; }
    }
#undef RT_AT
}

/* ---- messages */
static const char *const rt_errs[] = {
 [0] = "Success", [1] = "Operation not permitted", [2] = "No such file or directory", [3] = "No such process", [4] = "Interrupted system call",
 [5] = "Input/output error", [6] = "No such device or address", [7] = "Argument list too long", [8] = "Exec format error", [9] = "Bad file descriptor",
 [10] = "No child processes", [11] = "Resource temporarily unavailable", [12] = "Cannot allocate memory", [13] = "Permission denied", [14] = "Bad address",
 [15] = "Block device required", [16] = "Device or resource busy", [17] = "File exists", [18] = "Invalid cross-device link", [19] = "No such device",
 [20] = "Not a directory", [21] = "Is a directory", [22] = "Invalid argument", [23] = "Too many open files in system", [24] = "Too many open files",
 [25] = "Inappropriate ioctl for device", [26] = "Text file busy", [27] = "File too large", [28] = "No space left on device", [29] = "Illegal seek",
 [30] = "Read-only file system", [31] = "Too many links", [32] = "Broken pipe", [33] = "Numerical argument out of domain", [34] = "Numerical result out of range",
 [35] = "Resource deadlock avoided", [36] = "File name too long", [37] = "No locks available", [38] = "Function not implemented", [39] = "Directory not empty",
 [40] = "Too many levels of symbolic links", [42] = "No message of desired type", [43] = "Identifier removed", [61] = "No data available",
 [62] = "Timer expired", [71] = "Protocol error", [75] = "Value too large for defined data type", [84] = "Invalid or incomplete multibyte or wide character",
 [88] = "Socket operation on non-socket", [89] = "Destination address required", [90] = "Message too long", [91] = "Protocol wrong type for socket",
 [92] = "Protocol not available", [93] = "Protocol not supported", [94] = "Socket type not supported", [95] = "Operation not supported",
 [96] = "Protocol family not supported", [97] = "Address family not supported by protocol", [98] = "Address already in use", [99] = "Cannot assign requested address",
 [100] = "Network is down", [101] = "Network is unreachable", [102] = "Network dropped connection on reset", [103] = "Software caused connection abort",
 [104] = "Connection reset by peer", [105] = "No buffer space available", [106] = "Transport endpoint is already connected", [107] = "Transport endpoint is not connected",
 [110] = "Connection timed out", [111] = "Connection refused", [112] = "Host is down", [113] = "No route to host", [114] = "Operation already in progress",
 [115] = "Operation now in progress", [116] = "Stale file handle", [122] = "Disk quota exceeded", [125] = "Operation canceled",
};
char *strerror(int e) {
    static __thread char buf[48];
    if (e >= 0 && (size_t) e < sizeof rt_errs / sizeof *rt_errs && rt_errs[e]) return (char *) rt_errs[e];
    char *p = buf + sizeof buf; *--p = 0; unsigned v = (unsigned) (e < 0 ? -e : e); do *--p = (char) ('0' + v % 10); while (v /= 10);
    if (e < 0) *--p = '-';
    static const char pre[] = "Unknown error "; p -= sizeof pre - 1; memcpy(p, pre, sizeof pre - 1);
    return p;
}
static const char *const rt_sigs[] = { [1] = "Hangup", [2] = "Interrupt", [3] = "Quit", [4] = "Illegal instruction", [5] = "Trace/breakpoint trap", [6] = "Aborted",
 [7] = "Bus error", [8] = "Floating point exception", [9] = "Killed", [10] = "User defined signal 1", [11] = "Segmentation fault", [12] = "User defined signal 2",
 [13] = "Broken pipe", [14] = "Alarm clock", [15] = "Terminated", [16] = "Stack fault", [17] = "Child exited", [18] = "Continued", [19] = "Stopped (signal)",
 [20] = "Stopped", [21] = "Stopped (tty input)", [22] = "Stopped (tty output)", [23] = "Urgent I/O condition", [24] = "CPU time limit exceeded",
 [25] = "File size limit exceeded", [26] = "Virtual timer expired", [27] = "Profiling timer expired", [28] = "Window changed", [29] = "I/O possible",
 [30] = "Power failure", [31] = "Bad system call" };
char *strsignal(int s) { if (s > 0 && (size_t) s < sizeof rt_sigs / sizeof *rt_sigs && rt_sigs[s]) return (char *) rt_sigs[s]; return (char *) "Unknown signal"; }

/* ------------------------------------------------------------------ malloc: size classes carved from mmap'd slabs, big blocks mmap'd on their own */
typedef struct rt_blk { size_t sz; size_t cls; } rt_blk;      /* 16-byte header: payload size class (0 = direct mmap) */
#define RT_NCLS 28                                         /* class i holds 2^(i+4) bytes of payload: 16 B ... 2 GiB; above RT_BIG direct */
#define RT_BIG (1UL << 17)
static volatile int rt_mlock; static rt_blk *rt_free[RT_NCLS]; static char *rt_slab, *rt_slab_end;
void rt_malloc_atfork_child(void) { rt_mlock = 0; }
static void *rt_mmap_anon(size_t n) { long r = rt_raw(SYS_mmap, 0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); return (unsigned long) r > (unsigned long) -4096L ? 0 : (void *) r; }
void *malloc(size_t n) {
    if (n > (size_t) PTRDIFF_MAX - 4096) { errno = ENOMEM; return 0; }
    if (n > RT_BIG) {
        size_t tot = (n + sizeof(rt_blk) + 4095) & ~(size_t) 4095; rt_blk *b = rt_mmap_anon(tot);
        if (!b) { errno = ENOMEM; return 0; }
        b->sz = tot; b->cls = 0; return b + 1;
    }
    size_t cls = 0, cap = 16; while (cap < n) cap <<= 1, cls++;
    rt_lock(&rt_mlock);
    rt_blk *b = rt_free[cls];
    if (b) rt_free[cls] = *(rt_blk **) (b + 1);
    else {
        size_t need = cap + sizeof(rt_blk);
        if ((size_t) (rt_slab_end - rt_slab) < need) {
            size_t slab = 1UL << 21; char *s = rt_mmap_anon(slab);
            if (!s) { rt_unlock(&rt_mlock); errno = ENOMEM; return 0; }
            rt_slab = s; rt_slab_end = s + slab;
        }
        b = (rt_blk *) rt_slab; rt_slab += need; b->sz = cap; b->cls = cls + 1;
    }
    rt_unlock(&rt_mlock);
    return b + 1;
}
void free(void *p) {
    if (!p) return;
    rt_blk *b = (rt_blk *) p - 1;
    if (!b->cls) { rt_raw(SYS_munmap, b, b->sz, 0, 0, 0, 0); return; }
    rt_lock(&rt_mlock); *(rt_blk **) p = rt_free[b->cls - 1]; rt_free[b->cls - 1] = b; rt_unlock(&rt_mlock);
}
void *calloc(size_t a, size_t b) {
    size_t n; if (__builtin_mul_overflow(a, b, &n)) { errno = ENOMEM; return 0; }
    void *p = malloc(n); if (!p) return 0;
    /* through a pointer: the compiler turns an inlined malloc + memset into a call to calloc, which here would be this function */
    if (((rt_blk *) p - 1)->cls) { void *(*volatile ms)(void *, int, size_t) = memset; ms(p, 0, n); }        /* a fresh direct mapping is already zero */
    return p;
}
void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    rt_blk *b = (rt_blk *) p - 1; size_t old = b->cls ? b->sz : b->sz - sizeof(rt_blk);
    if (n <= old && (b->cls ? n > old / 2 || old <= 16 : n > old / 2)) return p;
    void *q = malloc(n); if (!q) return 0;
    memcpy(q, p, old < n ? old : n); free(p); return q;
}
int posix_memalign(void **out, size_t al, size_t n) {
    if (al <= 16) { void *p = malloc(n); if (!p) return ENOMEM; *out = p; return 0; }
    return EINVAL;
}
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); return p ? memcpy(p, s, n) : 0; }
char *strndup(const char *s, size_t m) { size_t n = strnlen(s, m); char *p = malloc(n + 1); if (!p) return 0; memcpy(p, s, n); p[n] = 0; return p; }

/* ------------------------------------------------------------------ environment */
char *getenv(const char *n) {
    size_t l = strlen(n); if (!environ || !l || strchr(n, '=')) return 0;
    for (char **e = environ; *e; e++) if (!strncmp(*e, n, l) && (*e)[l] == '=') return *e + l + 1;
    return 0;
}
static char **rt_env_own;                                  /* the environ array once pemu changed it (it was the kernel's stack until then) */
static int rt_env_grow(size_t n) {                          /* room for n entries + NULL */
    size_t cur = 0; if (environ) while (environ[cur]) cur++;
    if (n < cur) n = cur;
    if (environ == rt_env_own && rt_env_own) { char **p = realloc(rt_env_own, (n + 1) * sizeof(char *)); if (!p) return -1; rt_env_own = environ = p; return 0; }
    char **p = malloc((n + 1) * sizeof(char *)); if (!p) return -1;
    for (size_t i = 0; i < cur; i++) p[i] = environ[i];
    p[cur] = 0; rt_env_own = environ = p; return 0;
}
int setenv(const char *n, const char *v, int ow) {
    size_t l = strlen(n), vl = strlen(v);
    if (!l || strchr(n, '=')) { errno = EINVAL; return -1; }
    size_t cnt = 0, idx = (size_t) -1;
    if (environ) for (char **e = environ; *e; e++, cnt++) if (idx == (size_t) -1 && !strncmp(*e, n, l) && (*e)[l] == '=') idx = cnt;
    if (idx != (size_t) -1 && !ow) return 0;
    char *s = malloc(l + vl + 2); if (!s) return -1;
    memcpy(s, n, l); s[l] = '='; memcpy(s + l + 1, v, vl + 1);
    if (rt_env_grow(idx != (size_t) -1 ? cnt : cnt + 1) < 0) { free(s); return -1; }
    if (idx != (size_t) -1) { environ[idx] = s; return 0; }
    environ[cnt] = s; environ[cnt + 1] = 0; return 0;
}
int unsetenv(const char *n) {
    size_t l = strlen(n); if (!environ) return 0;
    if (environ != rt_env_own && rt_env_grow(0) < 0) return -1;
    char **o = environ, **d = environ;
    for (; *o; o++) if (!(!strncmp(*o, n, l) && (*o)[l] == '=')) *d++ = *o;
    *d = 0; return 0;
}
int clearenv(void) { if (environ != rt_env_own && rt_env_grow(0) < 0) return -1; environ[0] = 0; return 0; }
int putenv(char *s) {
    char *eq = strchr(s, '='); if (!eq) return unsetenv(s);
    char nm[256]; size_t l = (size_t) (eq - s); if (l >= sizeof nm) { errno = EINVAL; return -1; }
    memcpy(nm, s, l); nm[l] = 0; return setenv(nm, eq + 1, 1);
}
#endif /* PEMU_RT_MEM_H */

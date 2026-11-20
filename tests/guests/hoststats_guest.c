/* tests/guests/hoststats_guest.c - x86_64 Linux guest with no libc: its own _start, raw syscalls, a bump malloc over mmap, a tiny printf.
 * It makes exactly one write(1) of its own; tests/integration/host_stats.sh uses it to see what reaches the host kernel.  SPDX-License-Identifier: Apache-2.0 */
/* Program C tanpa libc: titik masuk, syscall, malloc, printf mini, semuanya milik sendiri. x86_64 Linux. */
typedef unsigned long size_t;
typedef long ssize_t;

/* 1. syscall mentah: instruksi `syscall`, nomor di rax, argumen di rdi rsi rdx r10 r8 r9 */
static long sc(long n, long a, long b, long c, long d, long e, long f) {
    register long r10 __asm__("r10") = d, r8 __asm__("r8") = e, r9 __asm__("r9") = f;
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;                      /* kernel mengembalikan -errno, persis seperti hs() di pemu */
}
#define SYS_write 1
#define SYS_mmap 9
#define SYS_exit_group 231

/* 2. fungsi yang compiler anggap selalu ada (gcc bisa menghasilkan panggilan ke memcpy/memset sendiri) */
void *memcpy(void *d, const void *s, size_t n) { char *p = d; const char *q = s; while (n--) *p++ = *q++; return d; }
void *memset(void *d, int c, size_t n) { char *p = d; while (n--) *p++ = (char) c; return d; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

/* 3. malloc paling sederhana: bump allocator di atas mmap (free = tidak melakukan apa-apa) */
static char *heap, *heap_end;
static void *xmalloc(size_t n) {
    n = (n + 15) & ~15UL;
    if (!heap || heap + n > heap_end) {
        size_t chunk = n > (1UL << 20) ? n : (1UL << 20);
        long p = sc(SYS_mmap, 0, chunk, 3 /*RW*/, 0x22 /*PRIVATE|ANON*/, -1, 0);
        if (p < 0 && p > -4096) return 0;
        heap = (char *) p; heap_end = heap + chunk;
    }
    void *r = heap; heap += n; return r;
}

/* 4. printf mini: hanya %s %d %x */
static void out(const char *s, size_t n) { sc(SYS_write, 1, (long) s, (long) n, 0, 0, 0); }
static void pr(const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    char buf[256]; size_t o = 0;
    for (; *fmt && o < 230; fmt++) {
        if (*fmt != '%') { buf[o++] = *fmt; continue; }
        fmt++;
        if (*fmt == 's') { const char *s = __builtin_va_arg(ap, const char *); while (*s && o < 230) buf[o++] = *s++; }
        else if (*fmt == 'd' || *fmt == 'x') {
            unsigned long v = (unsigned long) __builtin_va_arg(ap, long); unsigned b = *fmt == 'd' ? 10 : 16; char t[24]; int k = 0;
            if (*fmt == 'd' && (long) v < 0) { buf[o++] = '-'; v = -(long) v; }
            do { t[k++] = "0123456789abcdef"[v % b]; v /= b; } while (v);
            while (k) buf[o++] = t[--k];
        }
    }
    __builtin_va_end(ap); out(buf, o);
}

/* 5. main milik program; tidak ada argc/argv dari libc, jadi dibaca dari stack awal di _start */
static int app(int argc, char **argv) {
    char *m = xmalloc(64); memcpy(m, "dialokasikan dari mmap", 23);
    pr("halo tanpa libc\nargc=%d argv0=%s\n%s\nstrlen=%d hex=%x\n", (long) argc, argv[0], m, (long) strlen(m), 255L);
    return 0;
}

/* 6. titik masuk: kernel menaruh argc, argv[], envp[], auxv[] di stack; tidak ada yang menyiapkannya selain kita */
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %ebp,%ebp\n mov %rsp,%rdi\n and $-16,%rsp\n call start_c\n");
}
__attribute__((noreturn, used)) void start_c(long *sp) {
    int r = app((int) sp[0], (char **) (sp + 1));
    sc(SYS_exit_group, r, 0, 0, 0, 0, 0);
    for (;;) {}
}

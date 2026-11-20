/* host/rt/core.h - the runtime that replaces libc (step 2): process start, TLS, threads, fork, signals, setjmp, mutexes.
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is the only runtime: there is no libc build any more.  No system header is read (host/abi/include has the types, constants and struct
 * layouts) and every function pemu calls is defined in host/rt/ (rt_*.h).  Hosts: x86_64 (tested), aarch64 (written,
 * never run on hardware; its long double math is rt_quad.h, see README).
 *
 * Everything here is a plain global function with the libc name and prototype, so no caller changes. */
#ifndef PEMU_RT_CORE_H
#define PEMU_RT_CORE_H
#include <elf.h>
#include <link.h>
#include <linux/futex.h>
#include <linux/sched.h>
#include <linux/limits.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>

#define RT_ATTR_USED __attribute__((used))
#define RT_NOINLINE __attribute__((noinline))

/* kernel call with libc-style result */
#define rt_sys(nr, ...) host_syscall_e_nocount(nr, ##__VA_ARGS__)
#define host_syscall_e_nocount(nr, ...) ({ long rn_ = (long) (nr); rt_ret_(host_arg_(rn_, ##__VA_ARGS__, 0L, 0L, 0L, 0L, 0L, 0L)); })
static inline long rt_ret_(long r) { if ((unsigned long) r > (unsigned long) -4096L) { errno = (int) -r; return -1; } return r; }
#define rt_raw(nr, a, b, c, d, e, f) host_raw((long) (nr), (long) (a), (long) (b), (long) (c), (long) (d), (long) (e), (long) (f))

/* ------------------------------------------------------------------ thread control block */
struct rt_thr;
struct rt_tcb {
#if defined(__x86_64__)
    void *self;                         /* %fs:0 */
#endif
    int err;                            /* errno */
    volatile int tid;                   /* kernel tid; the kernel zeroes it when the thread is gone (CLONE_CHILD_CLEARTID) */
    volatile int state;                 /* threads: 1 running, 2 asked to exit (stack may be reused once tid == 0) */
    void *(*fn)(void *); void *arg;
    struct rt_thr *thr;
} __attribute__((aligned(16)));
struct rt_thr { struct rt_thr *next; char *base; size_t size; struct rt_tcb *tcb; };

static struct { Elf64_Addr img; size_t filesz, memsz, align; } rt_tls;     /* PT_TLS of this image */
static size_t rt_tls_len(void) { size_t a = rt_tls.align ? rt_tls.align : 1; return (rt_tls.memsz + a - 1) & ~(a - 1); }

#if defined(__x86_64__)
static inline struct rt_tcb *rt_tcb(void) { struct rt_tcb *t; __asm__("mov %%fs:0, %0" : "=r"(t)); return t; }
static inline void rt_set_tp(struct rt_tcb *t) { rt_raw(SYS_arch_prctl, 0x1002 /* ARCH_SET_FS */, t, 0, 0, 0, 0); }
#define RT_TLS_BELOW 1                  /* variant II: the TLS block ends at the thread pointer */
#elif defined(__aarch64__)
static inline struct rt_tcb *rt_tcb(void) { char *tp; __asm__("mrs %0, tpidr_el0" : "=r"(tp)); return (struct rt_tcb *) (tp - sizeof(struct rt_tcb)); }
static inline void rt_set_tp(struct rt_tcb *t) { __asm__ volatile("msr tpidr_el0, %0" :: "r"((char *) (t + 1))); }
#define RT_TLS_BELOW 0                  /* variant I: the TLS block starts 16 bytes above the thread pointer */
#endif

int *__errno_location(void) { return &rt_tcb()->err; }

/* lay out one thread's static TLS + TCB inside [mem, mem+len); returns the TCB */
static struct rt_tcb *rt_tls_setup(char *mem, size_t len) {
    size_t tl = rt_tls_len(), al = rt_tls.align > 64 ? rt_tls.align : 64;
    struct rt_tcb *t;
#if RT_TLS_BELOW
    char *tp = (char *) (((uintptr_t) mem + tl + al - 1) & ~(uintptr_t) (al - 1));
    t = (struct rt_tcb *) tp; (void) len;
    char *blk = tp - tl;
#else
    t = (struct rt_tcb *) (((uintptr_t) mem + al - 1) & ~(uintptr_t) (al - 1));
    char *blk = (char *) (t + 1) + 16 - 16;
    blk = (char *) (((uintptr_t) blk + 16 + (rt_tls.align ? rt_tls.align : 1) - 1) & ~(uintptr_t) ((rt_tls.align ? rt_tls.align : 1) - 1));
    (void) len;
#endif
    for (size_t i = 0; i < tl; i++) blk[i] = 0;
    for (size_t i = 0; i < rt_tls.filesz; i++) blk[i] = ((const char *) rt_tls.img)[i];
#if defined(__x86_64__)
    t->self = t;
#endif
    return t;
}
static size_t rt_tls_total(void) { return rt_tls_len() + (rt_tls.align > 64 ? rt_tls.align : 64) + sizeof(struct rt_tcb) + 4096; }

/* ------------------------------------------------------------------ futex mutex (the first int of pthread_mutex_t) */
static inline void rt_lock(volatile int *m) {
    int z = 0;
    if (__atomic_compare_exchange_n(m, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    while (__atomic_exchange_n(m, 2, __ATOMIC_ACQUIRE) != 0) rt_raw(SYS_futex, m, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 2, 0, 0, 0);
}
static inline void rt_unlock(volatile int *m) {
    if (__atomic_exchange_n(m, 0, __ATOMIC_RELEASE) == 2) rt_raw(SYS_futex, m, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
}
int pthread_mutex_lock(pthread_mutex_t *m) { rt_lock((volatile int *) m); return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { rt_unlock((volatile int *) m); return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m) { int z = 0; return __atomic_compare_exchange_n((volatile int *) m, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 0 : EBUSY; }
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void) a; *(volatile int *) m = 0; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m) { (void) m; return 0; }
int pthread_once(pthread_once_t *o, void (*fn)(void)) {            /* 0 new, 1 running, 2 done */
    volatile int *s = (volatile int *) o;
    for (;;) {
        int z = 0;
        if (__atomic_compare_exchange_n(s, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) { fn(); __atomic_store_n(s, 2, __ATOMIC_RELEASE); rt_raw(SYS_futex, s, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 0x7fffffff, 0, 0, 0); return 0; }
        if (z == 2) return 0;
        rt_raw(SYS_futex, s, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
    }
}

/* ------------------------------------------------------------------ threads */
int pthread_attr_init(pthread_attr_t *a) { size_t *p = (size_t *) a; p[0] = 1UL << 20; p[1] = 0; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void) a; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t n) { ((size_t *) a)[0] = n; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int d) { ((size_t *) a)[1] = (size_t) d; return 0; }

static volatile int rt_thr_lock; static struct rt_thr *rt_thr_list;

#if defined(__x86_64__)
__asm__(".text\n.global rt_clone\n.type rt_clone,@function\nrt_clone:\n"
        " xor %eax,%eax\n mov $56,%al\n mov %rdi,%r11\n mov %rdx,%rdi\n mov %r8,%rdx\n mov %r9,%r8\n mov 8(%rsp),%r10\n mov %r11,%r9\n"
        " and $-16,%rsi\n sub $8,%rsi\n mov %rcx,(%rsi)\n syscall\n test %eax,%eax\n jnz 1f\n xor %ebp,%ebp\n pop %rdi\n call *%r9\n"
        " mov %eax,%edi\n xor %eax,%eax\n mov $60,%al\n syscall\n hlt\n1: ret\n");
#elif defined(__aarch64__)
__asm__(".text\n.global rt_clone\n.type rt_clone,%function\nrt_clone:\n"
        " and x1,x1,#-16\n stp x0,x3,[x1,#-16]!\n uxtw x0,w2\n mov x2,x4\n mov x3,x5\n mov x4,x6\n mov x8,#220\n svc #0\n cbz x0,1f\n ret\n"
        "1: ldp x1,x0,[sp],#16\n blr x1\n mov x8,#93\n svc #0\n");
#endif
long rt_clone(int (*fn)(void *), void *stack, int flags, void *arg, int *ptid, void *tls, int *ctid);

static int rt_thread_entry(void *p) {
    struct rt_tcb *t = p;
    void *r = t->fn(t->arg);
    (void) r;
    t->state = 2; rt_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
    return 0;
}
void pthread_exit(void *r) {
    (void) r;
    struct rt_tcb *t = rt_tcb();
    t->state = 2;
    for (;;) rt_raw(SYS_exit, 0, 0, 0, 0, 0, 0);
}
int pthread_create(pthread_t *out, const pthread_attr_t *at, void *(*fn)(void *), void *arg) {
    size_t ss = at ? ((const size_t *) at)[0] : (1UL << 20), tot, pg = 4096, guard = pg;
    ss = (ss + pg - 1) & ~(pg - 1); tot = guard + ss + rt_tls_total();
    tot = (tot + pg - 1) & ~(pg - 1);
    struct rt_thr *th = 0;
    rt_lock(&rt_thr_lock);
    for (struct rt_thr *q = rt_thr_list; q; q = q->next)
        if (q->tcb->state == 2 && q->tcb->tid == 0 && q->size == tot) { th = q; break; }       /* a finished thread's stack */
    if (!th) {
        char *m = (char *) rt_raw(SYS_mmap, 0, tot, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if ((unsigned long) m > (unsigned long) -4096L) { rt_unlock(&rt_thr_lock); return EAGAIN; }
        rt_raw(SYS_mprotect, m, guard, PROT_NONE, 0, 0, 0);
        th = (struct rt_thr *) rt_raw(SYS_mmap, 0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if ((unsigned long) th > (unsigned long) -4096L) { rt_raw(SYS_munmap, m, tot, 0, 0, 0, 0); rt_unlock(&rt_thr_lock); return EAGAIN; }
        th->base = m; th->size = tot; th->next = rt_thr_list; rt_thr_list = th;
    }
    char *tlsmem = th->base + guard + ss;
    struct rt_tcb *t = rt_tls_setup(tlsmem, tot - guard - ss);
    t->err = 0; t->tid = 0; t->state = 1; t->fn = fn; t->arg = arg; t->thr = th; th->tcb = t;
    rt_unlock(&rt_thr_lock);
    long fl = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
    long r = rt_clone(rt_thread_entry, th->base + guard + ss, (int) fl, t, (int *) &t->tid,
#if defined(__x86_64__)
                      t,
#else
                      (char *) (t + 1),
#endif
                      (int *) &t->tid);
    if (r < 0) { t->state = 2; return (int) -r; }
    if (out) *out = (pthread_t) t;
    return 0;
}
pthread_t pthread_self(void) { return (pthread_t) rt_tcb(); }

/* ------------------------------------------------------------------ process start */
char **__environ; extern char **environ __attribute__((alias("__environ")));
static unsigned long rt_auxv_pagesz = 4096, rt_auxv_hwcap;
static unsigned long rt_auxv_random, rt_auxv_vdso;
static Elf64_auxv_t *rt_auxv_all;                                        /* the whole vector, for getauxval */
unsigned long getauxval(unsigned long type) {
    for (Elf64_auxv_t *a = rt_auxv_all; a && a->a_type != AT_NULL; a++) if (a->a_type == type) return a->a_un.a_val;
    errno = ENOENT; return 0;
}
static void (*rt_atexit_fn[32])(void); static int rt_atexit_n;
int atexit(void (*f)(void)) { if (rt_atexit_n >= 32) return -1; rt_atexit_fn[rt_atexit_n++] = f; return 0; }
void rt_stdio_flush_all(void); int rt_wr_all(int, const void *, size_t); int snprintf(char *, size_t, const char *, ...);
void _exit(int c) { for (;;) rt_raw(SYS_exit_group, c, 0, 0, 0, 0, 0); }
void _Exit(int c) { _exit(c); }
void exit(int c) { while (rt_atexit_n > 0) rt_atexit_fn[--rt_atexit_n](); rt_stdio_flush_all(); _exit(c); }

size_t strlen(const char *); int strcmp(const char *, const char *);
int main(int, char **);
extern const Elf64_Ehdr __ehdr_start __attribute__((visibility("hidden")));
extern Elf64_Dyn _DYNAMIC[] __attribute__((visibility("hidden")));

/* static-pie: nobody applies the relative relocations for us.  Runs first, uses no global pointer. */
static RT_NOINLINE RT_ATTR_USED void rt_relocate(void) {
    char *base = (char *) &__ehdr_start;
    const Elf64_Rela *rela = 0; size_t sz = 0, ent = sizeof(Elf64_Rela);
    for (const Elf64_Dyn *d = _DYNAMIC; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_RELA) rela = (const Elf64_Rela *) (base + d->d_un.d_ptr);
        else if (d->d_tag == DT_RELASZ) sz = d->d_un.d_val;
        else if (d->d_tag == DT_RELAENT) ent = d->d_un.d_val;
    }
    for (size_t o = 0; rela && o + ent <= sz; o += ent) {
        const Elf64_Rela *r = (const Elf64_Rela *) ((const char *) rela + o);
#if defined(__x86_64__)
        if (ELF64_R_TYPE(r->r_info) == R_X86_64_RELATIVE) *(Elf64_Addr *) (base + r->r_offset) = (Elf64_Addr) (base + r->r_addend);
#else
        if (ELF64_R_TYPE(r->r_info) == R_AARCH64_RELATIVE) *(Elf64_Addr *) (base + r->r_offset) = (Elf64_Addr) (base + r->r_addend);
#endif
    }
}

/* ---- the kernel's vDSO: clock_gettime & co. without a trap (libc does this too; pemu's rdtsc and the guest's clocks hit it a lot) */
static long (*rt_vdso_cgt)(int, void *), (*rt_vdso_gtod)(void *, void *), (*rt_vdso_time)(void *);
static void rt_vdso_init(unsigned long ehdr) {
    if (!ehdr) return;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) ehdr; const Elf64_Phdr *ph = (const Elf64_Phdr *) (ehdr + eh->e_phoff);
    char *base = 0; const Elf64_Dyn *dyn = 0; int have_load = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && !have_load) { base = (char *) (ehdr + ph[i].p_offset - ph[i].p_vaddr); have_load = 1; }
        else if (ph[i].p_type == PT_DYNAMIC) dyn = (const Elf64_Dyn *) (ehdr + ph[i].p_offset);
    }
    if (!have_load || !dyn) return;
    const Elf64_Sym *sym = 0; const char *str = 0; const unsigned *hash = 0, *gnu = 0;
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_SYMTAB) sym = (const Elf64_Sym *) (base + d->d_un.d_ptr);
        else if (d->d_tag == DT_STRTAB) str = base + d->d_un.d_ptr;
        else if (d->d_tag == DT_HASH) hash = (const unsigned *) (base + d->d_un.d_ptr);
        else if (d->d_tag == DT_GNU_HASH) gnu = (const unsigned *) (base + d->d_un.d_ptr);
    }
    if (!sym || !str) return;
    unsigned n = 0;
    if (hash) n = hash[1];
    else if (gnu) {                                                     /* count symbols: the last chain of the highest bucket */
        unsigned nb = gnu[0], so = gnu[1], bl = gnu[2]; const unsigned *bk = gnu + 4 + bl * 2, *ch = bk + nb; unsigned mx = 0;
        for (unsigned i = 0; i < nb; i++) if (bk[i] > mx) mx = bk[i];
        if (mx >= so) { while (!(ch[mx - so] & 1)) mx++; n = mx + 1; } else n = so;
    }
    for (unsigned i = 0; i < n; i++) {
        if (!sym[i].st_shndx || !sym[i].st_value || (ELF64_ST_TYPE(sym[i].st_info) != STT_FUNC)) continue;
        const char *nm = str + sym[i].st_name; void *f = base + sym[i].st_value;
        if (!strcmp(nm, "__vdso_clock_gettime")) rt_vdso_cgt = f;
        else if (!strcmp(nm, "__vdso_gettimeofday")) rt_vdso_gtod = f;
        else if (!strcmp(nm, "__vdso_time")) rt_vdso_time = f;
    }
}

typedef void (*rt_initfn)(int, char **, char **);
extern rt_initfn __preinit_array_start[] __attribute__((visibility("hidden"))), __preinit_array_end[] __attribute__((visibility("hidden")));
extern rt_initfn __init_array_start[] __attribute__((visibility("hidden"))), __init_array_end[] __attribute__((visibility("hidden")));
extern void (*__fini_array_start[])(void) __attribute__((visibility("hidden"))), (*__fini_array_end[])(void) __attribute__((visibility("hidden")));
static void rt_run_fini(void) { for (void (**f)(void) = __fini_array_end; f > __fini_array_start;) (*--f)(); }

RT_ATTR_USED static void rt_start_c(long *sp) {
    rt_relocate();
    int argc = (int) sp[0]; char **argv = (char **) (sp + 1), **envp = argv + argc + 1, **e = envp;
    while (*e) e++;
    const Elf64_Phdr *ph = 0; size_t phn = 0; char *base = (char *) &__ehdr_start;
    rt_auxv_all = (Elf64_auxv_t *) (e + 1);
    for (Elf64_auxv_t *a = (Elf64_auxv_t *) (e + 1); a->a_type != AT_NULL; a++) {
        if (a->a_type == AT_PHDR) ph = (const Elf64_Phdr *) a->a_un.a_val;
        else if (a->a_type == AT_PHNUM) phn = a->a_un.a_val;
        else if (a->a_type == AT_PAGESZ) rt_auxv_pagesz = a->a_un.a_val;
        else if (a->a_type == AT_HWCAP) rt_auxv_hwcap = a->a_un.a_val;
        else if (a->a_type == AT_RANDOM) rt_auxv_random = a->a_un.a_val;
        else if (a->a_type == AT_SYSINFO_EHDR) rt_auxv_vdso = a->a_un.a_val;
    }
    for (size_t i = 0; ph && i < phn; i++)
        if (ph[i].p_type == PT_TLS) { rt_tls.img = (Elf64_Addr) (base + ph[i].p_vaddr); rt_tls.filesz = ph[i].p_filesz; rt_tls.memsz = ph[i].p_memsz; rt_tls.align = ph[i].p_align; }
    size_t tot = (rt_tls_total() + 4095) & ~(size_t) 4095;
    char *m = (char *) rt_raw(SYS_mmap, 0, tot, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((unsigned long) m > (unsigned long) -4096L) _exit(127);
    struct rt_tcb *t = rt_tls_setup(m, tot);
    rt_set_tp(t);
    environ = envp;
    rt_vdso_init(rt_auxv_vdso);
    atexit(rt_run_fini);
    for (rt_initfn *f = __preinit_array_start; f < __preinit_array_end; f++) (*f)(argc, argv, envp);
    for (rt_initfn *f = __init_array_start; f < __init_array_end; f++) (*f)(argc, argv, envp);     /* constructors (pemu registers its OS table in one) */
    exit(main(argc, argv));
}
#if defined(__x86_64__)
__asm__(".text\n.global _start\n.type _start,@function\n_start:\n xor %ebp,%ebp\n mov %rsp,%rdi\n and $-16,%rsp\n call rt_start_c\n hlt\n");
#elif defined(__aarch64__)
__asm__(".text\n.global _start\n.type _start,%function\n_start:\n mov x29,#0\n mov x30,#0\n mov x0,sp\n and sp,x0,#-16\n bl rt_start_c\n brk #0\n");
#endif

/* ------------------------------------------------------------------ fork */
extern void rt_malloc_atfork_child(void);
pid_t fork(void) {
    rt_stdio_flush_all();
    long r = rt_raw(SYS_clone, SIGCHLD, 0, 0, 0, 0, 0);
    if (r == 0) { rt_malloc_atfork_child(); rt_thr_lock = 0; return 0; }
    return (pid_t) rt_ret_(r);
}

/* ------------------------------------------------------------------ signals (struct sigaction <-> the kernel's) */
/* the words of a sigset_t, whatever the libc calls its member (glibc __val, musl __bits, bionic __bits): it is a plain array of unsigned long */
#define RT_SSW(p) ((unsigned long *) (p))
struct rt_ksa { void *handler; unsigned long flags; void (*restorer)(void); unsigned long mask; };
#if defined(__x86_64__)
__asm__(".text\n.global rt_restore\n.type rt_restore,@function\nrt_restore:\n mov $15,%eax\n syscall\n");
#define RT_SA_RESTORER 0x04000000
#elif defined(__aarch64__)
__asm__(".text\n.global rt_restore\n.type rt_restore,%function\nrt_restore:\n mov x8,#139\n svc #0\n");
#define RT_SA_RESTORER 0
#endif
void rt_restore(void);
int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    struct rt_ksa k, ko;
    if (act) {
        k.handler = (void *) act->sa_handler; k.flags = (unsigned long) act->sa_flags | RT_SA_RESTORER; k.restorer = rt_restore;
        k.mask = RT_SSW(&act->sa_mask)[0];
    }
    long r = rt_raw(SYS_rt_sigaction, sig, act ? &k : 0, old ? &ko : 0, 8, 0, 0);
    if (r < 0) { errno = (int) -r; return -1; }
    if (old) {
        for (size_t i = 0; i < sizeof old->sa_mask / sizeof(long); i++) RT_SSW(&old->sa_mask)[i] = 0;
        old->sa_handler = (void (*)(int)) ko.handler; old->sa_flags = (int) ko.flags & ~RT_SA_RESTORER; old->sa_restorer = 0; RT_SSW(&old->sa_mask)[0] = ko.mask;
    }
    return 0;
}
void (*signal(int sig, void (*h)(int)))(int) {
    struct sigaction sa, o; sa.sa_handler = h; sa.sa_flags = SA_RESTART;
    for (size_t i = 0; i < sizeof sa.sa_mask / sizeof(long); i++) RT_SSW(&sa.sa_mask)[i] = 0;
    if (sigaction(sig, &sa, &o) < 0) return SIG_ERR;
    return o.sa_handler;
}
int sigprocmask(int how, const sigset_t *set, sigset_t *old) {
    unsigned long k, ko;
    if (set) k = RT_SSW(set)[0];
    long r = rt_raw(SYS_rt_sigprocmask, how, set ? &k : 0, old ? &ko : 0, 8, 0, 0);
    if (r < 0) { errno = (int) -r; return -1; }
    if (old) { for (size_t i = 0; i < sizeof *old / sizeof(long); i++) RT_SSW(old)[i] = 0; RT_SSW(old)[0] = ko; }
    return 0;
}
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) { int e = errno; int r = sigprocmask(how, set, old); int er = r ? errno : 0; errno = e; return er; }
int sigemptyset(sigset_t *s) { for (size_t i = 0; i < sizeof *s / sizeof(long); i++) RT_SSW(s)[i] = 0; return 0; }
int sigfillset(sigset_t *s) { for (size_t i = 0; i < sizeof *s / sizeof(long); i++) RT_SSW(s)[i] = ~0UL; return 0; }
int sigaddset(sigset_t *s, int n) { if (n < 1 || n > 64) { errno = EINVAL; return -1; } RT_SSW(s)[(n - 1) / 64] |= 1UL << ((n - 1) % 64); return 0; }
int sigdelset(sigset_t *s, int n) { if (n < 1 || n > 64) { errno = EINVAL; return -1; } RT_SSW(s)[(n - 1) / 64] &= ~(1UL << ((n - 1) % 64)); return 0; }
int sigismember(const sigset_t *s, int n) { if (n < 1 || n > 64) { errno = EINVAL; return -1; } return (int) ((RT_SSW(s)[(n - 1) / 64] >> ((n - 1) % 64)) & 1); }
int sigaltstack(const stack_t *ss, stack_t *old) { return (int) rt_sys(SYS_sigaltstack, ss, old); }
int raise(int sig) { return (int) rt_sys(SYS_tgkill, rt_raw(SYS_getpid, 0, 0, 0, 0, 0, 0), rt_raw(SYS_gettid, 0, 0, 0, 0, 0, 0), sig); }
int kill(pid_t p, int sig) { return (int) rt_sys(SYS_kill, p, sig); }
void abort(void) {
    sigset_t s; sigemptyset(&s); sigaddset(&s, SIGABRT); sigprocmask(SIG_UNBLOCK, &s, 0);
    raise(SIGABRT);
    signal(SIGABRT, SIG_DFL); raise(SIGABRT);
    for (;;) rt_raw(SYS_exit_group, 127, 0, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------ setjmp / longjmp (own register layout; sigjmp_buf keeps glibc's size) */
#if defined(__x86_64__)
__asm__(".text\n.global __sigsetjmp\n.type __sigsetjmp,@function\n__sigsetjmp:\n"
        " mov %rbx,(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n mov %r15,40(%rdi)\n"
        " lea 8(%rsp),%rdx\n mov %rdx,48(%rdi)\n mov (%rsp),%rdx\n mov %rdx,56(%rdi)\n"
        " mov %esi,64(%rdi)\n test %esi,%esi\n jz 1f\n"
        " push %rdi\n lea 72(%rdi),%rdx\n xor %edi,%edi\n xor %esi,%esi\n mov $8,%r10d\n mov $14,%eax\n syscall\n pop %rdi\n"
        "1: xor %eax,%eax\n ret\n"
        ".global siglongjmp\n.type siglongjmp,@function\nsiglongjmp:\n"
        " mov %esi,%eax\n test %eax,%eax\n jnz 2f\n inc %eax\n2:\n"
        " cmpl $0,64(%rdi)\n jz 3f\n"
        " push %rax\n push %rdi\n mov $2,%edi\n"
        " mov (%rsp),%rsi\n lea 72(%rsi),%rsi\n xor %edx,%edx\n mov $8,%r10d\n mov $14,%eax\n syscall\n pop %rdi\n pop %rax\n"
        "3: mov (%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n mov 40(%rdi),%r15\n"
        " mov 48(%rdi),%rsp\n jmp *56(%rdi)\n"
        ".global longjmp\n.type longjmp,@function\nlongjmp:\n jmp siglongjmp\n"
        ".global __longjmp_chk\n.type __longjmp_chk,@function\n__longjmp_chk:\n jmp siglongjmp\n"
        ".global _setjmp\n.type _setjmp,@function\n_setjmp:\n xor %esi,%esi\n jmp __sigsetjmp\n"
        ".global setjmp\n.type setjmp,@function\nsetjmp:\n xor %esi,%esi\n jmp __sigsetjmp\n");
#elif defined(__aarch64__)
__asm__(".text\n.global __sigsetjmp\n.type __sigsetjmp,%function\n__sigsetjmp:\n"
        " stp x19,x20,[x0,#0]\n stp x21,x22,[x0,#16]\n stp x23,x24,[x0,#32]\n stp x25,x26,[x0,#48]\n stp x27,x28,[x0,#64]\n stp x29,x30,[x0,#80]\n"
        " mov x2,sp\n str x2,[x0,#96]\n stp d8,d9,[x0,#104]\n stp d10,d11,[x0,#120]\n stp d12,d13,[x0,#136]\n stp d14,d15,[x0,#152]\n"
        " str w1,[x0,#168]\n cbz w1,1f\n mov x3,x0\n mov x0,#0\n mov x1,#0\n add x2,x3,#176\n mov x3,#8\n mov x8,#135\n svc #0\n"
        "1: mov w0,#0\n ret\n"
        ".global siglongjmp\n.type siglongjmp,%function\nsiglongjmp:\n"
        " mov w19,w1\n cbnz w19,2f\n mov w19,#1\n2: ldr w2,[x0,#168]\n cbz w2,3f\n mov x4,x0\n mov x0,#2\n add x1,x4,#176\n mov x2,#0\n mov x3,#8\n mov x8,#135\n svc #0\n mov x0,x4\n"
        "3: ldp x21,x22,[x0,#16]\n ldp x23,x24,[x0,#32]\n ldp x25,x26,[x0,#48]\n ldp x27,x28,[x0,#64]\n ldp x29,x30,[x0,#80]\n ldr x2,[x0,#96]\n mov sp,x2\n"
        " ldp d8,d9,[x0,#104]\n ldp d10,d11,[x0,#120]\n ldp d12,d13,[x0,#136]\n ldp d14,d15,[x0,#152]\n ldp x19,x20,[x0,#0]\n mov w0,w19\n ret\n"
        ".global longjmp\n.type longjmp,%function\nlongjmp:\n b siglongjmp\n"
        ".global __longjmp_chk\n.type __longjmp_chk,%function\n__longjmp_chk:\n b siglongjmp\n"
        ".global _setjmp\n.type _setjmp,%function\n_setjmp:\n mov w1,#0\n b __sigsetjmp\n"
        ".global setjmp\n.type setjmp,%function\nsetjmp:\n mov w1,#0\n b __sigsetjmp\n");
#endif
#endif /* PEMU_RT_CORE_H */

/* src/os/linux/thread.inc.h - threads and clone / fork / clone3; shared by every guest. Needs guest_set_sp / guest_set_tls / guest_set_retval from guest/<arch>/linux/guest.inc.h. SPDX-License-Identifier: Apache-2.0 */
/* ---- threads: every guest thread is one host pthread running its own interpreter loop ---- */
static void run(CPU*c);
typedef struct { CPU cpu; u64 flags, ptid, ctid; volatile int*tidout; struct pemu_sc_task sct; } ThArg;
static CPU g_dead_cpu;                                  /* a host signal that arrives after the CPU of an exiting thread is freed lands here (PENDP() is g_cur->pw) */
static void thread_exit(CPU*c) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_thread_exit();
#endif
    if (g_clear_tid) {
        __atomic_store_n((u32*) g_clear_tid, 0, __ATOMIC_SEQ_CST);
        host_syscall_e(HOP_futex, g_clear_tid, FUTEX_WAKE, 1, NULL, NULL, 0);
    }
    g_cur = &g_dead_cpu; __asm__ volatile("" ::: "memory");   /* before the free: no signal handler may write c->pw any more */
    free(c);                                              /* c is the ThArg (cpu is its first member) */
    host_thread_exit();
}
static void*thread_main(void*p) {
    ThArg*t = p; CPU*c = &t->cpu;
    g_cur = c; g_is_thread = 1;
    pemu_sct = t->sct;                                                 /* the guest thread starts with the filters of its creator */
    int tid = (int) host_syscall_e(HOP_gettid);
    if (t->flags & 0x00100000) *(u32*) t->ptid = (u32) tid;            /* PARENT_SETTID, before the guest runs */
    if (t->flags & 0x01000000) *(u32*) t->ctid = (u32) tid;            /* CHILD_SETTID */
    if (t->flags & 0x00200000) g_clear_tid = t->ctid;                  /* CHILD_CLEARTID */
    __atomic_store_n(t->tidout, tid, __ATOMIC_SEQ_CST);                 /* t may be freed once the thread exits: do not touch it after run() */
    run(c);
    return NULL;
}
/* From now on emulate x86 memory ordering: another agent (a second guest thread, or the kernel writing an io_uring ring) is
 * about to touch guest memory concurrently, and an aarch64 host would otherwise reorder the interpreted and translated accesses. */
static void mt_barriers(void) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_mt_enable();                        /* translated code from now on carries the memory barriers of x86 ordering */
#endif
    __atomic_store_n(&g_mt, 1, __ATOMIC_SEQ_CST);        /* see mrd/mwr */
}
static long clone_thread(CPU*c, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls) {
    if ((flags & 0x100) == 0 || (flags & 0x800) == 0) return -EINVAL;   /* threads need CLONE_VM | CLONE_SIGHAND */
    ThArg*t = malloc(sizeof *t);
    if (!t) return -ENOMEM;
    mt_barriers();
    t->sct = pemu_sct; t->cpu = *c; t->cpu.pw = 0; t->flags = flags; t->ptid = ptid; t->ctid = ctid; volatile int tidv = 0; t->tidout = &tidv;
    if (stack) guest_set_sp(&t->cpu, stack);
    if (flags & 0x00080000) guest_set_tls(&t->cpu, tls);                           /* SETTLS */
    guest_set_retval(&t->cpu, 0);
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 1UL << 20);
    pthread_t th; int e = host_thread_create(&th, &at, thread_main, t);
    pthread_attr_destroy(&at);
    if (e) { free(t); return -e; }
    int tid;
    while (!(tid = __atomic_load_n(&tidv, __ATOMIC_SEQ_CST))) sched_yield();
    return tid;
}

/* clone / fork / vfork / clone3 for a guest that does not share the address space (CLONE_THREAD goes to clone_thread). isclone: the
 * stack, tls and tid arguments are meaningful. pidfd: address for CLONE_PIDFD (clone3 only), or 0. */
static long clone_common(CPU*c, int isclone, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls, u64 pidfd, int*noset) {
    if (isclone && (flags & PEMU_CLONE_NEWMASK)) return PLOG_REFUSE(EPERM, "clone: new namespaces are refused (EPERM)");               /* as clone3 below: no namespaces, and no silent success either */
    if (flags & 0x10000 /*CLONE_THREAD*/) return clone_thread(c, flags, stack, ptid, ctid, tls);
#ifdef HAVE_JIT
    if (g_jit_on) jit_atfork(0);                  /* no other thread may hold the JIT lock across the fork */
#endif
    long pid = host_fork(); int fe = errno;
#ifdef HAVE_JIT
    if (g_jit_on) jit_atfork(pid == 0 ? 2 : 1);
#endif
    if (pid < 0) return -fe;
    if (pid == 0) {
        pemu_atfork_child();                                                         /* a lock (or half-done table edit) of another thread at fork time would stay locked in the child */
        if (isclone && stack) guest_set_sp(c, stack);
        if (isclone && (flags & 0x01000000)) *(u32*) ctid = (u32) host_getpid();      /* CHILD_SETTID */
        if (isclone && (flags & 0x00080000)) guest_set_tls(c, tls);                       /* SETTLS */
        if (flags & (1ULL << 32)) {                                               /* CLONE_CLEAR_SIGHAND (clone3): handled signals go back to SIG_DFL */
            for (int sg = 1; sg <= 64; sg++) {
                if (sg == SIGKILL || sg == SIGSTOP || sg == SIGSYS || gsa[sg].handler <= 1) continue;
                struct sigaction sd; memset(&sd, 0, sizeof sd); sd.sa_handler = SIG_DFL;
                sigaction(sg, &sd, NULL); memset(&gsa[sg], 0, sizeof gsa[sg]);
            }
        }
        guest_set_retval(c, 0); *noset = 1;
    } else {
        if (isclone && (flags & 0x00100000)) *(u32*) ptid = (u32) pid;           /* PARENT_SETTID */
        if (pidfd) {
#ifdef HOP_pidfd_open
            long fd = hs(HOP_pidfd_open, pid, 0, 0, 0, 0, 0);
            if (fd < 0) { host_kill((pid_t) pid, SIGKILL); host_waitpid((pid_t) pid, NULL, 0); return fd; }
            *(int*) pidfd = (int) fd;
#else
            host_kill((pid_t) pid, SIGKILL); host_waitpid((pid_t) pid, NULL, 0); return -ENOSYS;
#endif
        }
    }
    return pid;
}

/* clone3(struct clone_args *args, size_t size): translated to the same paths as clone. Things the emulator cannot give are refused
 * instead of silently ignored: namespaces (EPERM, as for an unprivileged process), set_tid, CLONE_INTO_CGROUP. CLONE_CLEAR_SIGHAND is
 * what glibc's posix_spawn passes. */
static long do_clone3(CPU*c, const u8*uargs, u64 size, int*noset) {
    struct { u64 flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls, set_tid, set_tid_size, cgroup; } ca;
    if (!uargs) return -EFAULT;
    if (size < 64 /*CLONE_ARGS_SIZE_VER0*/) return -EINVAL;
    if (size > 4096) return -E2BIG;
    memset(&ca, 0, sizeof ca);
    memcpy(&ca, uargs, size < sizeof ca ? (size_t) size : sizeof ca);
    for (u64 i = sizeof ca; i < size; i++) if (uargs[i]) return -E2BIG;       /* unknown trailing fields must be zero */
    if ((ca.flags >> 32) & ~1ULL) return PLOG_REFUSE(EINVAL, "clone3: CLONE_INTO_CGROUP / unknown flags are not supported (EINVAL)");   /* CLONE_INTO_CGROUP and unknown flags */
    if ((ca.flags & (1ULL << 32)) && (ca.flags & 0x800 /*CLONE_SIGHAND*/)) return -EINVAL;
    if (ca.exit_signal > 0xff || ca.set_tid_size) return ca.set_tid_size ? PLOG_REFUSE(EINVAL, "clone3: set_tid is not supported (EINVAL)") : -EINVAL;
    if (ca.flags & 0x7e0200a0ULL) return PLOG_REFUSE(EPERM, "clone3: new namespaces are refused (EPERM)");                               /* CLONE_NEW{NS,CGROUP,UTS,IPC,USER,PID,NET,TIME} */
    if ((ca.stack != 0) != (ca.stack_size != 0)) return -EINVAL;
    u64 sp = ca.stack ? ca.stack + ca.stack_size : 0;                          /* clone3 passes the lowest address, clone the top */
    if (ca.flags & 0x10000 /*CLONE_THREAD*/) {
        if (ca.exit_signal || (ca.flags & 0x1000 /*CLONE_PIDFD*/)) return -EINVAL;
        return clone_thread(c, ca.flags, sp, ca.parent_tid, ca.child_tid, ca.tls);
    }
    return clone_common(c, 1, ca.flags, sp, ca.parent_tid, ca.child_tid, ca.tls, (ca.flags & 0x1000) ? ca.pidfd : 0, noset);
}


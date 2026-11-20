/* src/os/linux/main.inc.h - command line, process set-up, main; shared by every 64-bit guest. SPDX-License-Identifier: Apache-2.0
 * Needs from guest/<arch>/linux/guest.inc.h (included before): GUEST_DESCRIPTION, GUEST_PLATFORM, GUEST_HWCAP, guest_install_tramp(), guest_cpu_start(sp, entry),
 * and from the Linux layer the objects `cpu`, run(), g_tramp; hsegv / hsys come from os/linux/signal.inc.h. */
int main(int argc, char**argv) {
    plog_init();                                                          /* .pemu/pemu.log: where it goes is decided before anything touches the environment */
    const char*argv0 = NULL; int a = 1, pr, nojit = 0;
    { int rc = pemu_subcmd(&argc, &argv); if (rc >= 0) return rc; }       /* login / install / uninstall / list */
#ifdef HAVE_JIT
    { const char*ev = getenv("PEMU_JIT"); if (ev && !strcmp(ev, "0")) nojit = 1; }
#else
    (void) nojit;
#endif
    for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++) {
        const char*o = argv[a];
        if (!strcmp(o, "--")) { a++; break; }
        if (!strcmp(o, "-0") && a + 1 < argc) argv0 = argv[++a];
        else if (!strcmp(o, "-strace")) g_trace = 1;
        else if (!strcmp(o, "-nojit")) nojit = 1;
        else if (!strcmp(o, "--version")) { printf("pemu %s (%s)\n", PEMU_RELEASE, GUEST_DESCRIPTION); return 0; }
        else if ((pr = pemu_opt(argc, argv, &a)) != 0) { if (pr < 0) return 2; }
        else { fprintf(stderr, "pemu: unknown option %s\n", o); return 2; }
    }
    if (a >= argc) { pemu_usage(stderr); return 2; }
    if (pemu_finish() < 0) return 1;
    if (pemu_sc_import() < 0) return 127;                /* --seccomp-fd: the filters of the image that exec'd this one (fail closed) */
    if (pemu_start_monitor() < 0) return 1;
    if (getenv("PEMU_STRACE")) g_trace = 1;
    ssize_t sl = readlink("/proc/self/exe", g_self, sizeof g_self - 1);
    if (sl > 0) g_self[sl] = 0; else strcpy(g_self, argv[0]);
    const char*prog = argv[a]; char**pav = argv + a; int pac = argc - a;
    char hostprog[PEMU_HP], gprog[PATH_MAX]; const char*loadp = prog;
    char xp[64];
    if (pemu_execfd >= 0) {                              /* --exec-fd: the file was handed over open (execveat of a memfd or deleted file) */
        snprintf(xp, sizeof xp, "/proc/self/fd/%d", pemu_execfd); loadp = xp;
        if (pemu_on) snprintf(pemu_exe, sizeof pemu_exe, "%s", prog);
    } else if (pemu_on) {
        char**nv; long e = pemu_exec_resolve_cmd(prog, pav, &nv, hostprog, gprog);
        if (e < 0) { pemu_err("pemu: %s: %s\n", prog, strerror((int) -e)); return 127; }
        pav = nv; pac = 0; while (pav[pac]) pac++;
        loadp = hostprog; prog = gprog; snprintf(pemu_exe, sizeof pemu_exe, "%s", gprog); g_exe_ok = 1;
    } else {                                             /* no rootfs: guest and host paths are the same; /proc/self/exe must name the program, not the emulator */
        char rp[PATH_MAX];
        if (realpath(prog, rp)) { snprintf(pemu_exe, sizeof pemu_exe, "%s", rp); g_exe_ok = 1; }
    }
    plog_set_prog(prog);                                 /* .pemu/pemu.log: lines carry the guest program name */
    char*sete = getenv("PEMU_SET_ENV"); if (sete) { sete = strdup(sete); char*sp1; for (char*t = strtok_r(sete, ",", &sp1); t; t = strtok_r(NULL, ",", &sp1)) { char*eq = strchr(t, '='); if (eq) { *eq = 0; setenv(t, eq + 1, 1); } } }
    ElfInfo prg = {0}, itp = {0};
    { int lr = load_elf(loadp, &prg); if (pemu_execfd >= 0) { host_close(pemu_execfd); pemu_execfd = -1; } if (lr < 0) return 127; }
    u64 entry = prg.entry;
    if (prg.has_interp) {
        const char*ip = prg.interp; char ih[PEMU_HP];
        if (pemu_on) { long e = pemu_path(AT_FDCWD, ip, 1, ih, sizeof ih, NULL); if (e < 0) { pemu_err("pemu: %s: %s\n", ip, strerror((int) -e)); return 127; } if (e == 0) ip = ih; }
        if (load_elf(ip, &itp) < 0) return 127;
    }
    if (prg.has_interp) entry = itp.entry;
    brk_end = (u64) host_mmap(NULL, 1ULL << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    brk_base = brk_cur = brk_end; brk_end += 1ULL << 30;
    g_tramp = (u64) host_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    guest_install_tramp();
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = hsegv; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    { struct sigaction ss; memset(&ss, 0, sizeof ss); ss.sa_sigaction = hsys; ss.sa_flags = SA_SIGINFO; sigemptyset(&ss.sa_mask); sigaction(SIGSYS, &ss, NULL); }
    size_t ssz = 8UL << 20;
    u64 top = (u64) host_mmap(NULL, ssz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0) + ssz;
    u64 sp = top;
    u64 execfn = push_str(&sp, prog);
    int ac = pac, ec = 0; while (environ[ec]) ec++;
    u64*as = calloc((size_t) ac + 1, 8), *es = calloc((size_t) ec + 1, 8);
    for (int k = ec - 1; k >= 0; k--) es[k] = push_str(&sp, environ[k]);
    for (int k = ac - 1; k >= 0; k--) as[k] = push_str(&sp, k == 0 && argv0 ? argv0 : pav[k]);
    if (ac > 0) {                                        /* /proc/self/cmdline: the argv strings, contiguous on the stack as in the kernel */
        const char*lastp = (ac == 1 && argv0) ? argv0 : pav[ac - 1];
        g_arg_lo = as[0]; g_arg_hi = as[ac - 1] + strlen(lastp) + 1;
    }
    u64 plat = push_str(&sp, GUEST_PLATFORM);
    sp &= ~15ULL; sp -= 16; u64 rnd = sp; if (host_syscall_e(HOP_getrandom, (void*) rnd, 16, 0) < 0) memset((void*) rnd, 0x5a, 16);
    u64 aux[][2] = {
        {AT_PHDR, prg.phdr}, {AT_PHENT, sizeof(Elf64_Phdr)}, {AT_PHNUM, prg.phnum}, {AT_PAGESZ, 4096},
        {AT_BASE, itp.base}, {AT_FLAGS, 0}, {AT_ENTRY, prg.entry}, {AT_UID, pemu_at_uid()}, {AT_EUID, pemu_at_euid()},
        {AT_GID, pemu_at_gid()}, {AT_EGID, pemu_at_egid()}, {AT_HWCAP, GUEST_HWCAP}, {AT_CLKTCK, 100}, {AT_SECURE, 0},
        {AT_RANDOM, rnd}, {AT_PLATFORM, plat}, {AT_EXECFN, execfn}, {AT_HWCAP2, 0}, {AT_NULL, 0}};
    int na = (int)(sizeof aux / sizeof aux[0]);
    u64 words = 1 + (u64) ac + 1 + (u64) ec + 1 + 2 * (u64) na;
    sp -= words * 8; sp &= ~15ULL;
    u64*w = (u64*) sp; int k = 0;
    w[k++] = (u64) ac;
    for (int j = 0; j < ac; j++) w[k++] = as[j];
    w[k++] = 0;
    for (int j = 0; j < ec; j++) w[k++] = es[j];
    w[k++] = 0;
    for (int j = 0; j < na; j++) { w[k++] = aux[j][0]; w[k++] = aux[j][1]; }
    guest_cpu_start(sp, entry);
#ifdef HAVE_JIT
    if (!nojit) {
        if (!pemu_jcache_set) { const char *ce = getenv("PEMU_JIT_CACHE"); snprintf(pemu_jcache, sizeof pemu_jcache, "%s", ce && *ce ? ce : "-"); pemu_jcache_set = 1; }   /* only the first pemu reads the environment */
        jit_cache_dir(strcmp(pemu_jcache, "-") ? pemu_jcache : NULL);
        if (jit_init() == 0) g_jit_on = 1;
    }
#endif
    run(&cpu);
    return 0;
}

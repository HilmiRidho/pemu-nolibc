/* src/proot/cmdline.inc.h - command-line options. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- command line: pemu's rootfs options (-r -b -w, and the optional proot ones: -i -k -v -l --kill-on-exit) */
static int pemu_add_pb(const char *h, const char *g, int must) {
    if (pemu_npb >= (int) (sizeof pemu_pb / sizeof *pemu_pb)) return -1;
    pemu_pb[pemu_npb].h = h; pemu_pb[pemu_npb].g = g; pemu_pb[pemu_npb].must = must; pemu_npb++;
    return 0;
}
/* -r: "chroot PATH" is "mount --bind PATH /".  An unusable path is an error. */
static int pemu_opt_root(const char *path) {
    char rh[PATH_MAX];
    if (!realpath(path, rh)) { pemu_note(PEMU_WARNING, "can't sanitize binding \"%s\": %s", path, strerror(errno)); return -1; }
    pemu_rootarg = path;
    return pemu_add_pb(path, "/", 1);
}
static int pemu_opt_bind(const char *spec) {                  /* -b HOST[:GUEST] */
    char *s = strdup(spec), *c = strchr(s, ':');
    const char *g = NULL; if (c) { *c = 0; g = c + 1; }
    return pemu_add_pb(s, g, 1);
}
static int pemu_opt_id(const char *spec) {                    /* -i UID:GID  (-0 is 0:0) */
    errno = 0; long u = strtol(spec, NULL, 10); unsigned uid = errno ? (unsigned) getuid() : (unsigned) u;
    unsigned gid = (unsigned) getgid();
    const char *gs = strchr(spec, ':');
    if (gs) { errno = 0; long g = strtol(gs + 1, NULL, 10); if (!errno) gid = (unsigned) g; }
    pemu_set_ids(uid, gid);
    return 0;
}
static int pemu_opt_verbose(const char *v) {
    char *end; errno = 0; long n = strtol(v, &end, 10);
    if (errno || end == v) { pemu_note(PEMU_ERROR, "option `-v` expects an integer value."); return -1; }
    pemu_verbose = (int) n;
    return 0;
}
static int pemu_opt_value(const char *o, const char *shortn, const char *longn, int argc, char **argv, int *a, const char **v) {
    size_t ll = longn ? strlen(longn) : 0;
    if ((shortn && !strcmp(o, shortn)) || (longn && !strcmp(o, longn))) { if (*a + 1 >= argc) return -1; *v = argv[++*a]; return 1; }
    if (longn && !strncmp(o, longn, ll) && o[ll] == '=') { *v = o + ll + 1; return 1; }
    return 0;
}
/* one command-line option; 1 = consumed (*a advanced), 0 = not a rootfs option, -1 = error.
 * Value options accept "--name value" and "--name=value". */
static int pemu_opt(int argc, char **argv, int *a) {
    const char *o = argv[*a], *v = NULL; int m;
#define VOPT(s, l, call) if ((m = pemu_opt_value(o, s, l, argc, argv, a, &v)) != 0) return m < 0 ? -1 : ((call) < 0 ? -1 : 1)
    VOPT("-r", "--rootfs", pemu_opt_root(v));
    VOPT("-b", "--bind", pemu_opt_bind(v));
    VOPT("-w", "--cwd", (snprintf(pemu_wd, sizeof pemu_wd, "%s", v), pemu_wd_set = 1, 0));
    VOPT("-i", "--change-id", pemu_opt_id(v));
    VOPT("-k", "--kernel-release", pemu_opt_kernel(v, 0));
    VOPT("-v", "--verbose", pemu_opt_verbose(v));
    VOPT(NULL, "--uts", pemu_opt_kernel(v, 1));                   /* hidden: state handed to a re-exec'd pemu */
    VOPT(NULL, "--ph", (pemu_ph_add(v), 0));                      /* hidden: placeholders the re-exec'd pemu now owns */
    VOPT(NULL, "--secure-open", (pemu_o2 = (*v == '1'), 0));      /* hidden: the openat2 decision of the first pemu (the environment may be gone) */
    VOPT(NULL, "--jit-cache", (snprintf(pemu_jcache, sizeof pemu_jcache, "%s", v), pemu_jcache_set = 1, 0));   /* hidden: the translation cache directory of the first pemu (src/jit/x86_64-aarch64/jcache.inc.h) */
    VOPT(NULL, "--log-file", plog_opt(v));                        /* hidden: where the first pemu writes .pemu/pemu.log (a guest may have changed its HOME) */
    VOPT(NULL, "--fence-root", (pemu_fence_root = (pid_t) strtol(v, NULL, 10), 0));   /* hidden: the monitor pid of the first pemu; its presence means "PID fence on" (pidfence.inc.h) */
#undef VOPT
    if (!strcmp(o, "--base-ids")) {                              /* hidden: the session's first fake uid,gid (before --ids) */
        unsigned x[2];
        if (*a + 1 >= argc || sscanf(argv[++*a], "%u,%u", x, x + 1) != 2) return -1;
        pemu_buid = x[0]; pemu_bgid = x[1]; pemu_bset = 1; return 1;
    }
    if (!strcmp(o, "--ids")) {                                    /* hidden: r,e,s,fs,r,e,s,fs */
        unsigned x[8];
        if (*a + 1 >= argc || sscanf(argv[++*a], "%u,%u,%u,%u,%u,%u,%u,%u", x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7) != 8) return -1;
        for (int i = 0; i < 4; i++) { pemu_uid[i] = x[i]; pemu_gid[i] = x[4 + i]; }
        if (!pemu_bset) { pemu_buid = pemu_uid[PEMU_R]; pemu_bgid = pemu_gid[PEMU_R]; pemu_bset = 1; }   /* old caller without --base-ids */
        pemu_fakeroot = 1; return 1;
    }
    if (!strcmp(o, "--root-id")) { pemu_set_ids(0, 0); return 1; }
    if (!strcmp(o, "--no-default-binds")) { pemu_nodefb = 1; return 1; }
    if (!strcmp(o, "--kill-on-exit")) { pemu_killall = 1; return 1; }
    if (!strcmp(o, "-l") || !strcmp(o, "--link2symlink")) { pemu_l2s = 1; return 1; }
    if (!strcmp(o, "--exec-fd")) {                                /* hidden: the program to run is an inherited fd */
        char *e; long fdv; if (*a + 1 >= argc) return -1;
        fdv = strtol(argv[++*a], &e, 10); if (*e || fdv < 3 || fdv > (1 << 20)) return -1;
        pemu_execfd = (int) fdv; return 1;
    }
    if (!strcmp(o, "--seccomp-fd")) {                            /* hidden: the guest's seccomp filters, as a sealed memfd (seccomp.inc.h) */
        char *e; long fdv; if (*a + 1 >= argc) return -1;
        fdv = strtol(argv[++*a], &e, 10); if (*e || fdv < 3 || fdv > (1 << 20)) return -1;
        pemu_sc_fd = (int) fdv; return 1;
    }
    if (!strcmp(o, "--reexec")) { pemu_reexec = 1; return 1; }
    if (!strcmp(o, "--top")) { pemu_wait = 1; return 1; }
    return 0;
}

/* guest working directory: a relative one ("." by default in proot mode) is relative to the host's current directory,
 * taken as a guest path.  When it cannot be entered, proot warns and starts in "/". */
static int pemu_init_cwd(void) {
    char base[PATH_MAX], path[PATH_MAX], gc[PATH_MAX], host[PEMU_HP];
    if (pemu_wd[0] != '/') { if (!host_getcwd(base, sizeof base)) { pemu_note(PEMU_ERROR, "getcwd: %s", strerror(errno)); return -1; } }
    else base[0] = 0;                                           /* absolute: no base */
    size_t bl = strlen(base), wl = strlen(pemu_wd);
    if (bl + wl + 4 > sizeof path) { pemu_note(PEMU_ERROR, "getcwd: %s", strerror(ENAMETOOLONG)); return -1; }
    memcpy(path, base, bl); if (bl) path[bl++] = '/'; memcpy(path + bl, pemu_wd, wl); memcpy(path + bl + wl, "/.", 3);
    long r = pemu_canon("/", path, 1, gc, 0);
    if (r == 0 && pemu_g2h(gc, host, NULL) < 0) r = -ENAMETOOLONG;
    if (r == 0 && host_chdir(host) < 0) r = -errno;
    if (r < 0) {
        pemu_note(PEMU_WARNING, "can't host_chdir(\"%s\") in the guest rootfs: %s", path, strerror((int) -r));
        pemu_note(PEMU_INFO, "default working directory is now \"/\"");
        strcpy(gc, "/");
        if (pemu_g2h(gc, host, NULL) < 0 || host_chdir(host) < 0) { pemu_note(PEMU_ERROR, "cannot enter rootfs: %s", strerror(errno)); return -1; }
    }
    strcpy(pemu_cwd, gc);
    setenv("PWD", gc, 1);                                       /* keep this special variable consistent */
    return 0;
}

static int pemu_depth(const char *g) { int d = 0; for (const char *c = g; *c; c++) d += *c == '/'; return d; }
/* Monitor process (proot "leave only when every guest process is gone").  The launching process stays behind as a small monitor: it is the
 * child subreaper, waits for the whole guest tree and returns the status of the first guest process; the FIRST GUEST PROCESS is its child.
 * Before, EVERY pemu image became a child subreaper and waited for its whole descendant tree before exiting (pemu_finish ran in each
 * re-exec'd guest process): a program that starts a daemon and exits (dbus-launch, dbus-daemon --fork, ssh-agent, gpg-agent ...) did not
 * exit until the daemon did, so its parent (a shell, startxfce4, dbus-run-session) hung on it, and a waitpid(-1) / wait saw orphaned
 * grandchildren that on a real kernel belong to init (native: ECHILD).  Only the monitor does that now. */
static volatile pid_t pemu_mon_first;
static void pemu_mon_fwd(int s) { if (pemu_mon_first > 0) host_kill(pemu_mon_first, s); }
/* called by main() right after pemu_finish(); 0 = continue as the first guest process, <0 = error.  Returns only in the guest. */
static int pemu_start_monitor(void) {
    if (!pemu_on || pemu_reexec || !pemu_wait) return 0;
    pid_t mon = host_getpid(), g = host_fork();
    if (g < 0) { pemu_note(PEMU_ERROR, "fork: %s", strerror(errno)); return -1; }
    if (g == 0) {                                             /* the first guest process */
        pemu_top = host_getpid(); pemu_wait = 0; prctl(PR_SET_CHILD_SUBREAPER, 0);
        prctl(PR_SET_PDEATHSIG, SIGKILL);                     /* the monitor was killed: do not leave the guest behind */
        if (getppid() != mon) host_exit(1);                       /* the monitor died before the prctl took effect */
        return 0;
    }
    pemu_mon_first = g;
    struct sigaction sa; memset(&sa, 0, sizeof sa); sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN; sigaction(SIGINT, &sa, NULL); sigaction(SIGQUIT, &sa, NULL);     /* terminal keys reach the guest through the process group */
    sa.sa_handler = pemu_mon_fwd;
    sigaction(SIGTERM, &sa, NULL); sigaction(SIGHUP, &sa, NULL); sigaction(SIGUSR1, &sa, NULL); sigaction(SIGUSR2, &sa, NULL);
    int fst = 0, done = 0, st; pid_t w;
    for (;;) {
        w = host_waitpid(-1, &st, 0);
        if (w < 0) { if (errno == EINTR) continue; break; }   /* ECHILD: nothing left */
        if (w == g && !done) {
            done = 1; fst = st;
            if (pemu_killall) { pemu_kill_and_reap(); break; }   /* repeated until no child is left (orphans keep arriving while we kill) */
        }
    }
    pemu_ph_cleanup();
    if (WIFSIGNALED(fst)) { signal(WTERMSIG(fst), SIG_DFL); host_raise(WTERMSIG(fst)); host_exit(128 + WTERMSIG(fst)); }
    host_exit(WEXITSTATUS(fst));
}
/* call once after option parsing; 0 = ok.  proot mode: the root defaults to "/" and there are no default binds. */
static int pemu_finish(void) {
    pemu_huid = getuid(); pemu_hgid = getgid(); pemu_top = host_getpid();
    int ri = -1;                                                /* the last binding whose guest path is "/" is the rootfs */
    for (int i = 0; i < pemu_npb; i++) {
        const char *g = pemu_pb[i].g ? pemu_pb[i].g : pemu_pb[i].h;
        if (g[0] != '/') continue;
        char t[PATH_MAX]; snprintf(t, sizeof t, "%s", g); pemu_norm(t);
        if (!strcmp(t, "/")) ri = i;
    }
    if (ri < 0) return 0;                                       /* no rootfs: pemu is only an emulator */
    const char *rp = pemu_pb[ri].h;
    if (!realpath(rp, pemu_root) || access(pemu_root, X_OK) < 0) { pemu_note(PEMU_ERROR, "rootfs %s: %s", rp, strerror(errno)); return -1; }
    pemu_nb = 0; pemu_add_bind(pemu_root, "/");
    pemu_on = 1;
    if (!pemu_reexec) {
        const char *pf = getenv("PEMU_PID_FENCE");
        if (!pf || strcmp(pf, "0")) pemu_fence_root = host_getpid();       /* this process stays behind as the monitor (pemu_start_monitor) */
    }
    if (pemu_fence_root > 0 && pemu_stat_ids(host_getpid(), NULL, NULL) < 0) {   /* /proc hidden (hidepid, a locked-down Android): the fence would refuse everything */
        pemu_note(PEMU_WARNING, "cannot read /proc/<pid>/stat: the PID fence is off");
        pemu_fence_root = 0;
    }
    if (!pemu_reexec) { pemu_wait = 1; prctl(PR_SET_CHILD_SUBREAPER, 1); }   /* the FIRST launch only (see pemu_start_monitor); a re-exec'd pemu
                                                                                is one more guest process, not the one that waits for the tree */
    if (!pemu_nodefb) {
        static const char *d[] = { "/dev", "/proc", "/sys" };
        for (int i = 0; i < 3; i++) pemu_bind_user(d[i], d[i], 0);
        if (access("/dev/shm", F_OK) != 0) { char t[PEMU_HP]; snprintf(t, sizeof t, "%s/tmp", pemu_root); host_mkdir(t, 01777); pemu_add_bind(t, "/dev/shm"); }
    }
    /* the other bindings: shallower guest paths first so deeper ones can be canonicalised through them; the command-line
     * order is kept otherwise (two bindings of one guest path: the last wins) */
    int ord[PEMU_MAXB * 2], no = 0;
    for (int i = 0; i < pemu_npb; i++) {
        const char *g = pemu_pb[i].g ? pemu_pb[i].g : pemu_pb[i].h;
        char t[PATH_MAX]; snprintf(t, sizeof t, "%s", g);
        if (g[0] == '/') { pemu_norm(t); if (!strcmp(t, "/")) continue; }
        int d = pemu_depth(t), k = no++;
        while (k > 0) {
            const struct pemu_pb *q = &pemu_pb[ord[k - 1]];
            if (pemu_depth(q->g ? q->g : q->h) <= d) break;
            ord[k] = ord[k - 1]; k--;
        }
        ord[k] = i;
    }
    for (int k = 0; k < no; k++) { const struct pemu_pb *b = &pemu_pb[ord[k]]; pemu_bind_user(b->h, b->g, b->must); }
    if (pemu_init_cwd() < 0) return -1;
    if (pemu_verbose > 0) {
        for (int i = 0; i < pemu_nb; i++) if (!pemu_b[i].off) {
            if (!strcmp(pemu_b[i].h, pemu_b[i].g)) pemu_note(PEMU_INFO, "binding = %s", pemu_b[i].h);
            else pemu_note(PEMU_INFO, "binding = %s:%s", pemu_b[i].h, pemu_b[i].g);
        }
        pemu_note(PEMU_INFO, "initial cwd = %s", pemu_cwd);
        pemu_note(PEMU_INFO, "verbose level = %d", pemu_verbose);
    }
    return 0;
}

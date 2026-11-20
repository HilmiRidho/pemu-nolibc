/* src/proot/exit.inc.h - exit handling: wait for the guest process tree. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- exit: proot waits until all the processes of the guest are gone and returns the status of the first one;
 * --kill-on-exit kills the others at once instead.  Orphans re-parent to the first process (child subreaper). */
/* Every descendant of `root`, found from the parent pids in /proc/<pid>/stat.  (/proc/<pid>/task/<tid>/children needs CONFIG_PROC_CHILDREN,
 * which many Android kernels lack: nothing was found there, nothing was killed and the monitor waited for ever.)  All of them are stopped
 * first (a stopped task cannot fork), then killed.  One scan is a snapshot, so the caller repeats it until nothing is left.
 * report != 0: say on stderr which processes are left.  Returns how many descendants were found. */
static int pemu_scan_tree(pid_t root, int report) {
    DIR *dp = opendir("/proc"); if (!dp) return 0;
    int cap = 256, n = 0; pid_t *pid = malloc(cap * sizeof *pid), *ppid = malloc(cap * sizeof *ppid); char *mark;
    struct dirent *e;
    if (!pid || !ppid) { free(pid); free(ppid); closedir(dp); return 0; }
    while ((e = readdir(dp))) {
        char *end; long v = strtol(e->d_name, &end, 10); if (*end || v <= 0) continue;
        char f[64], b[512]; snprintf(f, sizeof f, "/proc/%ld/stat", v);
        int fd = host_open(f, O_RDONLY | O_CLOEXEC); if (fd < 0) continue;
        ssize_t k = host_read(fd, b, sizeof b - 1); host_close(fd); if (k <= 0) continue;
        b[k] = 0; char *rp = strrchr(b, ')'); if (!rp) continue;       /* the name may hold spaces and ')' */
        int pp; if (sscanf(rp + 1, " %*c %d", &pp) != 1) continue;
        if (n == cap) {
            pid_t *a2 = realloc(pid, 2 * cap * sizeof *pid); if (!a2) break; pid = a2;
            pid_t *b2 = realloc(ppid, 2 * cap * sizeof *ppid); if (!b2) break; ppid = b2;
            cap *= 2;
        }
        pid[n] = (pid_t) v; ppid[n] = pp; n++;
    }
    closedir(dp);
    mark = calloc((size_t) n + 1, 1); int cnt = 0;
    if (mark) {
        for (int ch = 1; ch;) {                                       /* fixpoint: children of root, of those, ... */
            ch = 0;
            for (int i = 0; i < n; i++) {
                if (mark[i]) continue;
                int in = ppid[i] == root;
                for (int j = 0; !in && j < n; j++) in = mark[j] && pid[j] == ppid[i];
                if (in) { mark[i] = 1; ch = 1; cnt++; }
            }
        }
        for (int i = 0; i < n; i++) if (mark[i]) host_kill(pid[i], SIGSTOP);
        for (int i = 0; i < n; i++) if (mark[i]) {
            if (report) {
                char f[64], b[256] = "?"; snprintf(f, sizeof f, "/proc/%d/stat", (int) pid[i]);
                FILE *fp = fopen(f, "r"); if (fp) { if (!fgets(b, sizeof b, fp)) strcpy(b, "?"); fclose(fp); }
                b[strcspn(b, "\n")] = 0; fprintf(stderr, "pemu: still waiting for: %s\n", b);   /* pid (name) state ppid ... */
            }
            host_kill(pid[i], SIGKILL);
        }
    }
    free(mark); free(pid); free(ppid);
    return cnt;
}
/* --kill-on-exit: kill the whole tree below this process and reap it.  Orphans re-parent to us (child subreaper) while we kill, so
 * the pass is repeated until waitpid says ECHILD.  Zombies are reaped first, so a pass only walks live processes.  A process that
 * cannot be killed (D state, e.g. a stuck GPU / FUSE call) cannot be waited out here: after ~3 s it is named on stderr, once. */
static void pemu_kill_and_reap(void) {
    int rounds = 0;
    for (;;) {
        int st; pid_t w;
        do { w = host_waitpid(-1, &st, WNOHANG); } while (w > 0 || (w < 0 && errno == EINTR));
        if (w < 0) return;                                    /* ECHILD: nothing left */
        pemu_scan_tree(host_getpid(), rounds == 3000);
        rounds++;
        usleep(1000);                                         /* the killed ones become zombies; reaped on the next round */
    }
}
static void pemu_ph_cleanup(void) {
    if (pemu_ph_owner != host_getpid()) return;               /* only the process that owns them */
    for (int i = pemu_nph - 1; i >= 0; i--) {
        struct stat st;
        if (host_lstat(pemu_ph[i], &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) (void) host_rmdir(pemu_ph[i]);   /* rmdir only removes empty directories */
        else if (st.st_size == 0) (void) host_unlink(pemu_ph[i]);
    }
    pemu_nph = 0;
}
/* called just before the first guest process leaves with exit_group */
static void pemu_exit_hook(void) {
    plog_summary();                                                  /* .pemu/pemu.log: events that repeated in this process */
    host_stats_dump();                                               /* PEMU_HOST_STATS: how often this process called the host kernel (host/pemu_host.h) */
    if (pemu_wait && host_getpid() == pemu_top) {
        if (pemu_killall) pemu_kill_and_reap();
        int st; pid_t w;
        do { w = host_waitpid(-1, &st, 0); } while (w > 0 || (w < 0 && errno == EINTR));
    }
    pemu_ph_cleanup();
}

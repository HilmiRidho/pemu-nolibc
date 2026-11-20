/* src/os/linux/execve.inc.h - execve and execveat: the guest program is re-run under a new pemu image, a host program is exec'd directly; shared by every guest. SPDX-License-Identifier: Apache-2.0 */
static int is_guest_elf(const char*p) {                  /* an ELF file of this guest's architecture (PEMU_ELF_MACHINE) */
    int fd = host_open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char h[20];
    ssize_t n = host_read(fd, h, 20);
    host_close(fd);
    return n == 20 && !memcmp(h, "\177ELF", 4) && h[4] == 2 && *(u16*)(h + 18) == PEMU_ELF_MACHINE;
}
static long do_execve(const char*path, char**argv, char**envp) {
    char host[PEMU_HP], gprog[PATH_MAX]; char**av = argv; const char*hp = path, *gp = path;
    int xfd = -1;                                          /* fd of a file the new image cannot open by name (memfd, deleted file) */
    if (pemu_on) {                                         /* guest path -> host file, #! scripts expanded */
        long e = pemu_exec_resolve(path, argv, &av, host, gprog);
        if (e < 0) return e;
        hp = host; gp = gprog;
        if (pemu_exec_unnamed) xfd = pemu_exec_fdnum;
    } else xfd = pemu_exec_fdpath(path);
    if (!is_guest_elf(hp)) {
        if (PEMU_SC_ACTIVE()) return PLOG_REFUSE(ENOEXEC, "execve of a program that is not a guest binary under a guest seccomp filter: it would run unfiltered (ENOEXEC)");
        return hs(HOP_execve, (long) hp, (long) av, (long) envp, 0, 0, 0);
    }
    int n = 0; while (av && av[n]) n++;
    char**nv = calloc((size_t) n + 8 + (size_t) pemu_reexec_count(), sizeof *nv);
    int k = 0, keep = -1; char xs[16];
    nv[k++] = g_self;
    k = plog_reexec_opts(nv, k);                           /* .pemu/pemu.log: the new image writes to the same file even if the guest changed HOME */
    if (pemu_jcache_set) { nv[k++] = "--jit-cache"; nv[k++] = pemu_jcache; }      /* the translation cache decision, not the environment it was taken from */
    /* set-uid file: effective + saved id become the file's owner for the new image.  This must happen BEFORE pemu_reexec_opts() writes
       the --ids option: it was done after, so the new image always started with the old ids and a set-uid helper (dbus-daemon-launch-helper)
       ran with euid != 0 ("The permission of the setuid helper is not correct").  Undone below if the exec does not happen. */
    struct pemu_idsave ids; pemu_exec_ids(hp, &ids);
    if (pemu_on) k = pemu_reexec_opts(nv, k);
    if (xfd >= 0) {                                        /* hand the open file to the new image: it is closed there once loaded */
        keep = (int) host_fcntl(xfd, F_DUPFD, 3);               /* F_DUPFD: not close-on-exec */
        if (keep < 0) { int er = errno; pemu_exec_ids_undo(&ids); free(nv); return -er; }
        snprintf(xs, sizeof xs, "%d", keep); nv[k++] = "--exec-fd"; nv[k++] = xs;
    }
    int scfd = -1; char scs[16];                           /* the guest's seccomp filters follow the exec (seccomp.inc.h); no hand-over, no exec */
    { long se = pemu_sc_export(&scfd);
      if (se < 0) { pemu_exec_ids_undo(&ids); if (keep >= 0) host_close(keep); free(nv); return se; }
      if (scfd >= 0) { snprintf(scs, sizeof scs, "%d", scfd); nv[k++] = "--seccomp-fd"; nv[k++] = scs; } }
    nv[k++] = "-0"; nv[k++] = n ? av[0] : (char*) gp; nv[k++] = (char*) gp;
    for (int i = 1; i < n; i++) nv[k++] = av[i];
    /* g_self is a HOST binary (bionic on Android). If the guest's LD_LIBRARY_PATH / LD_PRELOAD
       reach it, the host linker picks up guest libs (e.g. rootfs libm.so, a GNU ld script) and
       dies with "bad ELF magic". Hide those two from the host exec and hand them over through
       PEMU_SET_ENV, which main() applies to the guest environment. */
    int ne = 0; while (envp && envp[ne]) ne++;
    char**nenv = calloc((size_t) ne + 2, sizeof *nenv);
    const char*ldl = NULL, *ldp = NULL, *oldset = NULL;
    int m = 0;
    for (int i = 0; i < ne; i++) {
        if (!strncmp(envp[i], "LD_LIBRARY_PATH=", 16)) ldl = envp[i] + 16;
        else if (!strncmp(envp[i], "LD_PRELOAD=", 11)) ldp = envp[i] + 11;
        else if (!strncmp(envp[i], "PEMU_SET_ENV=", 13)) oldset = envp[i] + 13;
        else nenv[m++] = envp[i];
    }
    char*setv = NULL;
    if (ldl || ldp || oldset) {
        size_t cap = 32 + (ldl ? strlen(ldl) : 0) + (ldp ? strlen(ldp) : 0) + (oldset ? strlen(oldset) : 0);
        setv = malloc(cap);
        if (setv) {
            int o = snprintf(setv, cap, "PEMU_SET_ENV=");
            if (oldset && *oldset) o += snprintf(setv + o, cap - (size_t) o, "%s,", oldset);
            if (ldp) o += snprintf(setv + o, cap - (size_t) o, "LD_PRELOAD=%s,", ldp);
            if (ldl) o += snprintf(setv + o, cap - (size_t) o, "LD_LIBRARY_PATH=%s,", ldl);
            if (o > 0 && setv[o - 1] == ',') setv[o - 1] = 0;
            nenv[m++] = setv;
        }
    }
    nenv[m] = NULL;
#ifdef HAVE_JIT
    if (g_jit_on) jcache_flush();                              /* what this image translated is kept for the next run (persistent translation cache) */
#endif
    long r = hs(HOP_execve, (long) g_self, (long) nv, (long) nenv, 0, 0, 0);
    pemu_exec_ids_undo(&ids);                                  /* still here: the exec failed */
    if (keep >= 0) host_close(keep);
    if (scfd >= 0) host_close(scfd);
    free(nv); free(nenv); free(setv);
    return r;
}

/* execveat(dirfd, path, argv, envp, flags) on top of do_execve: a path is needed to tell guest ELF files from host ones */
static long do_execveat(int dirfd, const char*path, char**argv, char**envp, long flags) {
    char buf[4096 + 64];
    if (flags & ~(long)(0x1000 | 0x100)) return -EINVAL;                       /* AT_EMPTY_PATH, AT_SYMLINK_NOFOLLOW */
    if (!path) return -EFAULT;
    if (!*path) {
        if (!(flags & 0x1000)) return -ENOENT;
        if (snprintf(buf, sizeof buf, "/proc/self/fd/%d", dirfd) >= (int) sizeof buf) return -ENAMETOOLONG;
        path = buf;
    } else {
        if (flags & 0x100) {
            struct stat st;
            if (hs(HOP_newfstatat, dirfd, (long) path, (long) &st, AT_SYMLINK_NOFOLLOW, 0, 0) == 0 && S_ISLNK(st.st_mode)) return -ELOOP;
        }
        if (path[0] != '/' && dirfd != AT_FDCWD) {
            if (snprintf(buf, sizeof buf, "/proc/self/fd/%d/%s", dirfd, path) >= (int) sizeof buf) return -ENAMETOOLONG;
            path = buf;
        }
    }
    return do_execve(path, argv, envp);
}


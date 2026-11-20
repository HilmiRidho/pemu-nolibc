/* src/guest/x86_64/linux/syscall.inc.h - x86_64 syscall dispatch: proot hook (pemu_sys), translation (gsys), do_syscall. SPDX-License-Identifier: Apache-2.0 */
/* ---- built-in proot hook: guest x86_64 syscalls that name a path or an identity.
 * Returns 1 and sets *res when it handled the call, 0 to let the normal translation run. */
static int pemu_sys(u64 n, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, long *res) {
#define RET(x) do { *res = (x); return 1; } while (0)
    switch (n) {                                       /* hostname / domainname with -k */
    case 41: if ((int) a0 == 16 && (int) a2 == 9) RET(-EPROTONOSUPPORT);   /* NETLINK_AUDIT: shadow (useradd) and sudo treat this as "no audit" and go on */
             break;
    case 170: case 171: { long r; if (pemu_set_uts_name(n == 171, (const char*) a0, (size_t) a1, &r)) RET(r);
                          RET(pemu_force(hs(n == 170 ? HOP_sethostname : HOP_setdomainname, (long) a0, (long) a1, 0, 0, 0, 0))); }
    }
    if (pemu_fakeroot) switch (n) {                    /* identity: proot's fake_id0 */
    case 102: RET(pemu_idcall(PEMU_GETUID, 0, 0, 0));      case 107: RET(pemu_idcall(PEMU_GETEUID, 0, 0, 0));
    case 104: RET(pemu_idcall(PEMU_GETGID, 0, 0, 0));      case 108: RET(pemu_idcall(PEMU_GETEGID, 0, 0, 0));
    case 118: RET(pemu_idcall(PEMU_GETRESUID, a0, a1, a2)); case 120: RET(pemu_idcall(PEMU_GETRESGID, a0, a1, a2));
    case 105: RET(pemu_idcall(PEMU_SETUID, a0, 0, 0));     case 106: RET(pemu_idcall(PEMU_SETGID, a0, 0, 0));
    case 113: RET(pemu_idcall(PEMU_SETREUID, a0, a1, 0));  case 114: RET(pemu_idcall(PEMU_SETREGID, a0, a1, 0));
    case 117: RET(pemu_idcall(PEMU_SETRESUID, a0, a1, a2)); case 119: RET(pemu_idcall(PEMU_SETRESGID, a0, a1, a2));
    case 122: RET(pemu_idcall(PEMU_SETFSUID, a0, 0, 0));   case 123: RET(pemu_idcall(PEMU_SETFSGID, a0, 0, 0));
    case 93: { long r = pemu_force_acc(hs(HOP_fchown, (long) a0, (long) pemu_chown_id(a1, pemu_uid, getuid()), (long) pemu_chown_id(a2, pemu_gid, getgid()), 0, 0, 0));
               if (r == 0) pemu_own_chown(NULL, 0, (int) a0, a1, a2);
               RET(r); }
    case 91: RET(pemu_force(hs(HOP_fchmod, (long) a0, (long) a1, 0, 0, 0, 0)));
    case 190: RET(pemu_force(hs(HOP_fsetxattr, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, 0)));
    case 160: if (!a1) break; RET(pemu_rlimit(0, (long) a0, (const void*) a1, NULL));      /* setrlimit: same layout as rlimit64 on x86_64 */
    case 302: if (!a2) break; RET(pemu_rlimit((long) a0, (long) a1, (const void*) a2, (void*) a3));
    case 126: RET(pemu_force_ns(hs(HOP_capset, (long) a0, (long) a1, 0, 0, 0, 0)));
    case 116: RET(pemu_force_ns(hs(HOP_setgroups, (long) a0, (long) a1, 0, 0, 0, 0)));
    }
    if (!pemu_on) {                                    /* no rootfs: only what a fake root needs forced */
        if (!pemu_fakeroot) return 0;
        const char *q0 = (const char*) a0, *q1 = (const char*) a1;
        switch (n) {
        case 92:  RET(pemu_callat(HOP_fchownat, AT_FDCWD, q0, 1, (long) a1, (long) a2, 0, 0));
        case 94:  RET(pemu_callat(HOP_fchownat, AT_FDCWD, q0, 0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0));
        case 260: RET(pemu_callat(HOP_fchownat, (long) a0, q1, !(a4 & 0x100), (long) a2, (long) a3, (long) a4, 0));
        case 133: RET(pemu_callat(HOP_mknodat, AT_FDCWD, q0, 0, (long) a1, (long) a2, 0, 0));
        case 259: RET(pemu_callat(HOP_mknodat, (long) a0, q1, 0, (long) a2, (long) a3, 0, 0));
        case 90:  RET(pemu_callat(HOP_fchmodat, AT_FDCWD, q0, 1, (long) a1, 0, 0, 0));
        case 268: RET(pemu_callat(HOP_fchmodat, (long) a0, q1, 1, (long) a2, 0, 0, 0));
        case 46:  return pemu_sock_hook(PEMU_S_SENDMSG, a0, a1, a2, a3, a4, a5, res);        /* SCM_CREDENTIALS / SO_PEERCRED ids */
        case 47:  return pemu_sock_hook(PEMU_S_RECVMSG, a0, a1, a2, a3, a4, a5, res);
        case 299: return pemu_sock_hook(PEMU_S_RECVMMSG, a0, a1, a2, a3, a4, a5, res);
        case 307: return pemu_sock_hook(PEMU_S_SENDMMSG, a0, a1, a2, a3, a4, a5, res);
        case 55:  return pemu_sock_hook(PEMU_S_GETSOCKOPT, a0, a1, a2, a3, a4, a5, res);
        case 188: RET(pemu_call1(HOP_setxattr, q0, 1, (long) a1, (long) a2, (long) a3, (long) a4, 0));
        case 189: RET(pemu_call1(HOP_lsetxattr, q0, 0, (long) a1, (long) a2, (long) a3, (long) a4, 0));
        }
        return 0;
    }
    const char *p0 = (const char*) a0, *p1 = (const char*) a1;
    switch (n) {
    case 217: RET(hs(HOP_getdents64, (long) a0, (long) a1, (long) a2, 0, 0, 0));
    case 2:   { long hf = fl_g2h((long) a1); if (is_cpuinfo(p0, hf)) return 0;
                RET(pemu_open(AT_FDCWD, p0, hf, (long) a2, 0)); }
    case 85:  RET(pemu_open(AT_FDCWD, p0, O_CREAT | O_WRONLY | O_TRUNC, (long) a1, 0));
    case 257: { long hf = fl_g2h((long) a2); if (is_cpuinfo(p1, hf)) return 0;
                RET(pemu_open((long) a0, p1, hf, (long) a3, 0)); }
    case 437: { u64 how[8] = {0}; u64 sz = a3;
                if (sz < 24) RET(-EINVAL);
                if (sz > sizeof how) RET(-E2BIG);
                memcpy(how, (const void*) a2, (size_t) sz); how[0] = (u64) fl_g2h((long) how[0]);
                if (is_cpuinfo(p1, (long) how[0])) return 0;
                RET(pemu_openat2((long) a0, p1, how, sz)); }
    case 452: RET(pemu_callat(HOP_fchmodat2, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 21:  RET(pemu_callat(HOP_faccessat, AT_FDCWD, p0, 1, (long) a1, 0, 0, 0));
    case 269: RET(pemu_callat(HOP_faccessat, (long) a0, p1, 1, (long) a2, 0, 0, 0));
    case 439: RET(pemu_callat(HOP_faccessat2, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 76:  RET(pemu_call1(HOP_truncate, p0, 1, (long) a1, 0, 0, 0, 0));
    case 79:  RET(pemu_getcwd((char*) a0, (long) a1));
    case 80:  RET(pemu_chdir(p0));
    case 81:  RET(pemu_fchdir((long) a0));
    case 161: RET(pemu_chroot(p0));
    case 165: RET(pemu_mount(p0, p1, (unsigned long) a3));       /* mount: only MS_BIND (edits the bind table) */
    case 166: RET(pemu_umount(p0));                              /* umount2 */
#ifdef HOP_fanotify_mark
    case 301: RET(pemu_fanotify_mark(HOP_fanotify_mark, (long) a0, (long) a1, (long) a2, (long) a3, (const char*) a4));
#endif
    case 82:  RET(pemu_call2at(HOP_renameat, AT_FDCWD, p0, 0, AT_FDCWD, p1, 0, 0));
    case 264: RET(pemu_call2at(HOP_renameat, (long) a0, p1, 0, (long) a2, (const char*) a3, 0, 0));
    case 316: RET(pemu_call2at(HOP_renameat2, (long) a0, p1, 0, (long) a2, (const char*) a3, 0, (long) a4));
    case 83:  RET(pemu_callat(HOP_mkdirat, AT_FDCWD, p0, 0, (long) a1, 0, 0, 0));
    case 258: RET(pemu_callat(HOP_mkdirat, (long) a0, p1, 0, (long) a2, 0, 0, 0));
    case 84:  RET(pemu_callat(HOP_unlinkat, AT_FDCWD, p0, 0, AT_REMOVEDIR, 0, 0, 0));
    case 87:  RET(pemu_callat(HOP_unlinkat, AT_FDCWD, p0, 0, 0, 0, 0, 0));
    case 263: RET(pemu_callat(HOP_unlinkat, (long) a0, p1, 0, (long) a2, 0, 0, 0));
    case 86:  RET(pemu_call2at(HOP_linkat, AT_FDCWD, p0, 0, AT_FDCWD, p1, 0, 0));
    case 265: RET(pemu_call2at(HOP_linkat, (long) a0, p1, !!(a4 & 0x400), (long) a2, (const char*) a3, 0, (long) a4));
    case 88:  RET(pemu_symlinkat(p0, AT_FDCWD, p1));
    case 266: RET(pemu_symlinkat(p0, (long) a1, (const char*) a2));
    case 89:  RET(pemu_readlinkat(AT_FDCWD, p0, (char*) a1, (long) a2));
    case 267: RET(pemu_readlinkat((long) a0, p1, (char*) a2, (long) a3));
    case 90:  RET(pemu_callat(HOP_fchmodat, AT_FDCWD, p0, 1, (long) a1, 0, 0, 0));
    case 268: RET(pemu_callat(HOP_fchmodat, (long) a0, p1, 1, (long) a2, 0, 0, 0));
    case 133: RET(pemu_callat(HOP_mknodat, AT_FDCWD, p0, 0, (long) a1, (long) a2, 0, 0));
    case 259: RET(pemu_callat(HOP_mknodat, (long) a0, p1, 0, (long) a2, (long) a3, 0, 0));
    case 280: RET(pemu_callat(HOP_utimensat, (long) a0, p1, !(a3 & 0x100), (long) a2, (long) a3, 0, 0));
    case 332: { struct pemu_own_ov ov = {0}; long r = pemu_callat_ov(HOP_statx, (long) a0, p1, !(a2 & 0x100), (long) a2, (long) a3, (long) a4, 0, &ov);
                if (r == 0 && pemu_fakeroot) { u32 *b = (u32*) a4; b[5] = ov.valid ? ov.uid : PEMU_UID(b[5]); b[6] = ov.valid ? ov.gid : PEMU_GID(b[6]); }
                RET(r); }
    case 137: RET(pemu_call1(HOP_statfs, p0, 1, (long) a1, 0, 0, 0, 0));
#ifdef HOP_setxattr
    case 188: RET(pemu_call1(HOP_setxattr, p0, 1, (long) a1, (long) a2, (long) a3, (long) a4, 0));
    case 189: RET(pemu_call1(HOP_lsetxattr, p0, 0, (long) a1, (long) a2, (long) a3, (long) a4, 0));
    case 191: RET(pemu_call1(HOP_getxattr, p0, 1, (long) a1, (long) a2, (long) a3, 0, 0));
    case 192: RET(pemu_call1(HOP_lgetxattr, p0, 0, (long) a1, (long) a2, (long) a3, 0, 0));
    case 194: RET(pemu_call1(HOP_listxattr, p0, 1, (long) a1, (long) a2, 0, 0, 0));
    case 195: RET(pemu_call1(HOP_llistxattr, p0, 0, (long) a1, (long) a2, 0, 0, 0));
    case 197: RET(pemu_call1(HOP_removexattr, p0, 1, (long) a1, 0, 0, 0, 0));
    case 198: RET(pemu_call1(HOP_lremovexattr, p0, 0, (long) a1, 0, 0, 0, 0));
#endif
    case 92:  RET(pemu_callat(HOP_fchownat, AT_FDCWD, p0, 1, (long) a1, (long) a2, 0, 0));
    case 94:  RET(pemu_callat(HOP_fchownat, AT_FDCWD, p0, 0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0));
    case 260: RET(pemu_callat(HOP_fchownat, (long) a0, p1, !(a4 & 0x100), (long) a2, (long) a3, (long) a4, 0));
    case 254: { char host[PEMU_HP]; long r = pemu_path(AT_FDCWD, p1, !(a2 & 0x02000000), host, sizeof host, NULL);
                if (r < 0) RET(r);
                RET(hs(HOP_inotify_add_watch, (long) a0, (long) (r == 0 ? host : p1), (long) a2, 0, 0, 0)); }
    case 49:  return pemu_sock_hook(PEMU_S_BIND, a0, a1, a2, a3, a4, a5, res);
    case 42:  return pemu_sock_hook(PEMU_S_CONNECT, a0, a1, a2, a3, a4, a5, res);
    case 51:  return pemu_sock_hook(PEMU_S_GETSOCKNAME, a0, a1, a2, a3, a4, a5, res);
    case 52:  return pemu_sock_hook(PEMU_S_GETPEERNAME, a0, a1, a2, a3, a4, a5, res);
    case 44:  return pemu_sock_hook(PEMU_S_SENDTO, a0, a1, a2, a3, a4, a5, res);
    case 45:  return pemu_sock_hook(PEMU_S_RECVFROM, a0, a1, a2, a3, a4, a5, res);
    case 46:  return pemu_sock_hook(PEMU_S_SENDMSG, a0, a1, a2, a3, a4, a5, res);
    case 47:  return pemu_sock_hook(PEMU_S_RECVMSG, a0, a1, a2, a3, a4, a5, res);
    case 299: return pemu_sock_hook(PEMU_S_RECVMMSG, a0, a1, a2, a3, a4, a5, res);
    case 307: return pemu_sock_hook(PEMU_S_SENDMMSG, a0, a1, a2, a3, a4, a5, res);
    case 55:  return pemu_sock_hook(PEMU_S_GETSOCKOPT, a0, a1, a2, a3, a4, a5, res);
    case 43:  return pemu_sock_hook(PEMU_S_ACCEPT, a0, a1, a2, a3, a4, a5, res);
    case 288: return pemu_sock_hook(PEMU_S_ACCEPT4, a0, a1, a2, a3, a4, a5, res);
    }
    return 0;
#undef RET
}

static long gsys(CPU*c, int*noset) {
    u64 n = c->r[RAX], a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10], a4 = c->r[8], a5 = c->r[9];
    long r, nr;
    if (PEMU_SC_ACTIVE()) {                                                  /* the guest's own seccomp filter, on the guest's numbers, before anything else (proot/seccomp.inc.h) */
        long sv; int sk = pemu_sc_enter(n, c->rip, a0, a1, a2, a3, a4, a5, &sv);
        if (sk == PEMU_SC_DONE) { plog_noted = 1; return sv; }
        if (sk == PEMU_SC_TRAP) {
            if (gsa[SIGSYS].handler <= 1) pemu_sc_die(SIGSYS, n, c->rip);   /* forced: default action when there is no handler, even if blocked or ignored */
            pemu_sc_siginfo(&pinfo[SIGSYS], (uint32_t) sv, n, c->rip);
            __atomic_or_fetch(PENDP(), 1ULL << SIGSYS, __ATOMIC_SEQ_CST);   /* delivered before the next instruction; rax still holds the syscall number, as on Linux */
            *noset = 1; plog_noted = 1; return -EPERM;
        }
        if (sk == PEMU_SC_KILLT && g_is_thread) thread_exit(c);             /* does not return */
        if (sk != PEMU_SC_RUN) pemu_sc_die((int) sv, n, c->rip);
    }
    if (pemu_sc_call(n, a0, a1, a2, a3, a4, &r)) return r;   /* prctl(NO_NEW_PRIVS / SECCOMP), seccomp() */
    if (pemu_policy(n, a0, a1, a2, a3, a4, &r)) return r;     /* the one refusal table (src/proot/deny.inc.h), with or without a rootfs */
    if (pemu_fence_call(n, a0, a1, a2, a3, &r)) return r;      /* only processes of the guest's own tree; mq names per rootfs (src/proot/pidfence.inc.h) */
    selfexe_patch(n, &a0, &a1);                                              /* /proc/self/exe -> the guest program (see is_selfexe) */
    if ((pemu_on || pemu_fakeroot || pemu_kset) && pemu_sys(n, a0, a1, a2, a3, a4, a5, &r)) return r;
    switch (n) {
#ifdef HAVE_JIT
    case 231: jit_report(); pemu_exit_hook(); return hs(HOP_exit_group, (long) a0, 0, 0, 0, 0, 0);
#else
    case 231: pemu_exit_hook(); return hs(HOP_exit_group, (long) a0, 0, 0, 0, 0, 0);
#endif
    case 60:
        if (g_is_thread) thread_exit(c);                /* does not return */
#ifdef HAVE_JIT
        jit_report();
#endif
        pemu_exit_hook();
        return hs(HOP_exit_group, (long) a0, 0, 0, 0, 0, 0);
    case 2: { long hf = fl_g2h((long) a1); if (is_cpuinfo((const char*) a0, hf)) return fake_proc_fd((const char*) a0, hf);
              return hs(HOP_openat, AT_FDCWD, (long) a0, hf, (long) a2, 0, 0); }
    case 85: return hs(HOP_openat, AT_FDCWD, (long) a0, O_CREAT | O_WRONLY | O_TRUNC, (long) a1, 0, 0);
    case 257: { long hf = fl_g2h((long) a2); if (is_cpuinfo((const char*) a1, hf)) return fake_proc_fd((const char*) a1, hf);
                return hs(HOP_openat, (long) a0, (long) a1, hf, (long) a3, 0, 0); }
#ifdef HOP_openat2
    case 437: {                                                                                             /* openat2: how->flags holds O_* flags, which differ between x86_64 and arm64 */
        u64 how[8] = {0}; u64 sz = a3;
        if (sz < 24) return -EINVAL;
        if (sz > sizeof how) return -E2BIG;
        memcpy(how, (const void*) a2, (size_t) sz); how[0] = (u64) fl_g2h((long) how[0]);
        if (is_cpuinfo((const char*) a1, (long) how[0])) return fake_proc_fd((const char*) a1, (long) how[0]);
        return hs(HOP_openat2, (long) a0, (long) a1, (long) how, (long) sz, 0, 0); }
#endif
    case 78: return g_getdents((long) a0, (u8*) a1, (long) a2);
    case 155: case 163: case 167: case 168: case 169: case 172: case 173: case 175: case 176: case 246: case 313: case 320:
        return PLOG_REFUSE(EPERM, "refused on purpose (EPERM): pivot_root, acct, swap, reboot, iopl, ioperm, modules, kexec never act on the host");                                /* pivot_root, acct, swapon/off, reboot, iopl, ioperm, init/finit/delete_module, kexec_load/kexec_file_load: never act on the host */
    case 4: return g_stat(AT_FDCWD, (const char*) a0, (u8*) a1, 0);
    case 6: return g_stat(AT_FDCWD, (const char*) a0, (u8*) a1, AT_SYMLINK_NOFOLLOW);
    case 262: return g_stat((long) a0, (const char*) a1, (u8*) a2, (long) a3);
    case 5: { struct stat st; r = hs(HOP_fstat, (long) a0, (long) &st, 0, 0, 0, 0); if (r == 0) { struct pemu_own_ov ov = {0}; l2s_fix_fd((int) a0, &st); pemu_own_fstat((int) a0, &ov, (uint64_t) st.st_dev); cv_stat(&st, (u8*) a1, &ov); } return r; }
    case 7: { struct timespec ts, *tp = NULL; if ((int) a2 >= 0) { ts.tv_sec = (int) a2 / 1000; ts.tv_nsec = ((int) a2 % 1000) * 1000000L; tp = &ts; }
              return hs(HOP_ppoll, (long) a0, (long) a1, (long) tp, 0, 8, 0); }
    case 21: return hs(HOP_faccessat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 22: return hs(HOP_pipe2, (long) a0, 0, 0, 0, 0, 0);
    case 43: return hs(HOP_accept4, (long) a0, (long) a1, (long) a2, 0, 0, 0);   /* accept == accept4 with flags 0. A plain accept (aarch64 202) is not in Android's app seccomp allowlist (bionic uses accept4), so it would come back ENOSYS and wineserver would spin on a ready socket */
    case 23: { struct timespec ts, *tp = NULL; if (a4) { struct timeval*tv = (struct timeval*) a4; ts.tv_sec = tv->tv_sec; ts.tv_nsec = tv->tv_usec * 1000L; tp = &ts; }
               return hs(HOP_pselect6, (long) a0, (long) a1, (long) a2, (long) a3, (long) tp, 0); }
    case 33: if (a0 == a1) return hs(HOP_fcntl, (long) a0, F_GETFD, 0, 0, 0, 0) < 0 ? -EBADF : (long) a1;
             return hs(HOP_dup3, (long) a0, (long) a1, 0, 0, 0, 0);
    case 292: return hs(HOP_dup3, (long) a0, (long) a1, fl_g2h((long) a2) & O_CLOEXEC, 0, 0, 0);
    case 34: return hs(HOP_ppoll, 0, 0, 0, 0, 8, 0);
    case 37: { struct itimerval it = {{0, 0}, {(time_t) a0, 0}}; return hs(HOP_setitimer, ITIMER_REAL, (long) &it, 0, 0, 0, 0); }
    case 82: return hs(HOP_renameat, AT_FDCWD, (long) a0, AT_FDCWD, (long) a1, 0, 0);
    case 83: return hs(HOP_mkdirat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 84: return hs(HOP_unlinkat, AT_FDCWD, (long) a0, AT_REMOVEDIR, 0, 0, 0);
    case 86: return hs(HOP_linkat, AT_FDCWD, (long) a0, AT_FDCWD, (long) a1, 0, 0);
    case 87: return hs(HOP_unlinkat, AT_FDCWD, (long) a0, 0, 0, 0, 0);
    case 88: return hs(HOP_symlinkat, (long) a0, AT_FDCWD, (long) a1, 0, 0, 0);
    case 89: if (!pemu_on && is_selfexe((const char*) a0)) return selfexe_readlink((char*) a1, (long) a2);
             return hs(HOP_readlinkat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 267: if (!pemu_on && is_selfexe((const char*) a1)) return selfexe_readlink((char*) a2, (long) a3);
              return hs(HOP_readlinkat, (long) a0, (long) a1, (long) a2, (long) a3, 0, 0);
    case 90: return hs(HOP_fchmodat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 92: return hs(HOP_fchownat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 94: return hs(HOP_fchownat, AT_FDCWD, (long) a0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0);
    case 235: { struct timespec ts[2], *tp = NULL; if (a1) { struct timeval*tv = (struct timeval*) a1; for (int i = 0; i < 2; i++) { ts[i].tv_sec = tv[i].tv_sec; ts[i].tv_nsec = tv[i].tv_usec * 1000L; } tp = ts; }
                return pemu_callat(HOP_utimensat, AT_FDCWD, (const char*) a0, 1, (long) tp, 0, 0, 0); }
    /* clock_gettime / gettimeofday / time go through the host's libc: it uses the host vDSO (a few ns) where a raw syscall costs a trap, and
     * on Android a seccomp filter run as well.  The guest has no vDSO of its own, so every one of its calls came here as a real syscall.
     * A NULL pointer keeps its -EFAULT; the libc call has no other failure the syscall did not have (bad clock id: -EINVAL). */
    case 228: { if (!a1) return -EFAULT; r = clock_gettime((clockid_t) a0, (struct timespec*) a1); return r < 0 ? -errno : 0; }
    case 96:  { if (!a0) { if (!a1) return 0; } r = gettimeofday((struct timeval*) a0, (void*) a1); return r < 0 ? -errno : 0; }
    case 201: { struct timespec ts; if (clock_gettime(CLOCK_REALTIME, &ts) < 0) return -errno; if (a0) *(s64*) a0 = ts.tv_sec; return (long) ts.tv_sec; }
    case 72:
        if (a1 == F_GETFL) { r = hs(HOP_fcntl, (long) a0, F_GETFL, 0, 0, 0, 0); return r < 0 ? r : fl_h2g(r); }
        if (a1 == F_SETFL) return hs(HOP_fcntl, (long) a0, F_SETFL, fl_g2h((long) a2), 0, 0, 0);
        return hs(HOP_fcntl, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 63: { struct utsname u; r = hs(HOP_uname, (long) &u, 0, 0, 0, 0, 0); if (r == 0) { pemu_uname_fix(&u, "x86_64"); memcpy((void*) a0, &u, sizeof u); } return r; }
    case 12:
        return pemu_brk(a0);
    case 131: {                                       /* sigaltstack(const stack_t *ss, stack_t *old): sp@0 flags@8 size@16 */
        int on = g_ss_size && c->r[RSP] >= g_ss_sp && c->r[RSP] - g_ss_sp < g_ss_size;
        u64 osp = g_ss_sp, osz = g_ss_size; u32 ofl = !g_ss_size ? 2 : on ? 1 : 0;
        if (a0) {
            u64 nsp = *(u64u*) a0, nsz = *(u64u*)(a0 + 16); u32 nfl = *(u32u*)(a0 + 8);
            if (on) return -EPERM;                                    /* cannot change it while running on it */
            if (nfl & ~(u32)(2 | 0x80000000u)) return -EINVAL;        /* SS_DISABLE, SS_AUTODISARM */
            if (nfl & 2) { g_ss_sp = 0; g_ss_size = 0; }
            else { if (nsz < 2048 /*MINSIGSTKSZ*/) return -ENOMEM; g_ss_sp = nsp; g_ss_size = nsz; }
        }
        if (a1) { *(u64u*) a1 = osp; *(u32u*)(a1 + 8) = ofl; *(u32u*)(a1 + 12) = 0; *(u64u*)(a1 + 16) = osz; }
        return 0; }
    case 111: return hs(HOP_getpgid, 0, 0, 0, 0, 0, 0);
    case 158:                                         /* arch_prctl */
        switch (a0) {
        case 0x1002: c->fs = a1; return 0;
        case 0x1001: c->gs = a1; return 0;
        case 0x1003: *(u64*) a1 = c->fs; return 0;
        case 0x1004: *(u64*) a1 = c->gs; return 0;
        case 0x1011: return 1;                        /* ARCH_GET_CPUID: cpuid is always enabled */
        case 0x1012: return a1 ? 0 : -ENODEV;         /* ARCH_SET_CPUID: cpuid faulting cannot be emulated, like a CPU without it */
        default: return -EINVAL;
        }
    case 157:                                         /* prctl: PR_SET_SYSCALL_USER_DISPATCH (Wine) would arm the host, not the guest: refuse like an old kernel */
        if ((int) a0 == 59) return PLOG_REFUSE(EINVAL, "prctl(PR_SET_SYSCALL_USER_DISPATCH) would arm the host, refused (EINVAL)");
        /* PR_SET_SECCOMP / PR_GET_SECCOMP / NO_NEW_PRIVS never get here: pemu_sc_call() answers them at the top of gsys() */
        return hs(HOP_prctl, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, 0);
    case 13: {                                        /* rt_sigaction */
        int s = (int) a0;
        if (s < 1 || s > 64 || s == SIGKILL || s == SIGSTOP) return -EINVAL;
        struct gsa old = gsa[s];
        if (a1) {
            struct gsa nw = *(struct gsa*) a1;
            struct sigaction sa; memset(&sa, 0, sizeof sa);
            sigfillset(&sa.sa_mask);
            if (nw.handler == 0) sa.sa_handler = SIG_DFL;
            else if (nw.handler == 1) sa.sa_handler = SIG_IGN;
            else { sa.sa_sigaction = hsig; sa.sa_flags = SA_SIGINFO | (int)(nw.flags & SA_RESTART); }
            if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE) { if (nw.handler > 1) sa.sa_sigaction = hsegv; sa.sa_flags |= SA_NODEFER; }
            if (s != SIGSYS && sigaction(s, &sa, NULL) < 0) return -errno;     /* SIGSYS stays with hsys() */
            gsa[s] = nw;
        }
        if (a2) *(struct gsa*) a2 = old;
        return 0;
    }
    case 14: {                                        /* rt_sigprocmask: never let the guest block SIGSYS on the host, or a seccomp kill cannot be turned into ENOSYS (see hsys) */
        long set = (long) a1; u64 nm;
        if (a1 && (int) a0 != 1) { nm = *(u64*) a1 & ~(1ULL << (SIGSYS - 1)); set = (long) &nm; }
        return hs(HOP_rt_sigprocmask, (long) a0, set, (long) a2, 8, 0, 0); }
    case 15: g_sigreturn(c); *noset = 1; return 0;
    case 232: case 281: {                             /* epoll_wait / epoll_pwait: guest events are packed (12 bytes) */
        int max = (int) a2; if (max <= 0 || max > 4096) return -EINVAL;
        struct epoll_event*ev = malloc(sizeof *ev * (size_t) max);
        if (!ev) return -ENOMEM;
        r = hs(HOP_epoll_pwait, (long) a0, (long) ev, max, (long) a3, n == 281 ? (long) a4 : 0, 8);
        for (long i = 0; i < r; i++) { u8*g = (u8*) a1 + 12 * i; *(u32*) g = ev[i].events; *(u64u*)(g + 4) = ev[i].data.u64; }
        free(ev); return r;
    }
    case 213: return hs(HOP_epoll_create1, 0, 0, 0, 0, 0, 0);
    case 233: {                                       /* epoll_ctl: the x86_64 guest struct is packed (events@0, data@4, 12 bytes); the aarch64 host struct is not (data@8) */
        struct epoll_event ev, *p = NULL;
        if (a3) { memset(&ev, 0, sizeof ev); ev.events = *(u32u*) a3; ev.data.u64 = *(u64u*)(a3 + 4); p = &ev; }
        return hs(HOP_epoll_ctl, (long) a0, (long) a1, (long) a2, (long) p, 0, 0); }
    case 284: return hs(HOP_eventfd2, (long) a0, 0, 0, 0, 0, 0);
    case 56: return clone_common(c, 1, a0 & 0xffffffffULL, a1, a2, a3, a4, 0, noset);                                      /* clone */
    case 57: case 58: return clone_common(c, 0, SIGCHLD, 0, 0, 0, 0, 0, noset);                            /* fork / vfork */
    case 435: return do_clone3(c, (const u8*) a0, a1, noset);
    case 322: return do_execveat((int) a0, (const char*) a1, (char**) a2, (char**) a3, (long) a4);
    case 59: return do_execve((const char*) a0, (char**) a1, (char**) a2);
    case 9:
        if (a3 & 0x40 /*MAP_32BIT*/ && !a0) return mmap32(a1, a2, a3 & ~0x40UL & ~(u64) MAP_FIXED, a4, a5);
        return hs(HOP_mmap, (long) a0, (long) a1, (long) a2, (long)(a3 & ~0x40UL), (long) a4, (long) a5);
#ifdef HOP_shmctl
    case 31: return hs(HOP_shmctl, (long) a0, (long)(a1 & ~0x100UL), (long) a2, 0, 0, 0);                   /* IPC_64 is implied on 64-bit; the generic ABI rejects the flag */
#endif
#ifdef HOP_semctl
    case 66: return hs(HOP_semctl, (long) a0, (long) a1, (long)(a2 & ~0x100UL), (long) a3, 0, 0);
#endif
#ifdef HOP_msgctl
    case 71: return hs(HOP_msgctl, (long) a0, (long)(a1 & ~0x100UL), (long) a2, 0, 0, 0);
#endif
    case 132: { struct timespec ts[2], *tp = NULL;                                                          /* utime(path, struct utimbuf*) */
                if (a1) { const s64*u = (const s64*) a1; ts[0].tv_sec = (time_t) u[0]; ts[0].tv_nsec = 0; ts[1].tv_sec = (time_t) u[1]; ts[1].tv_nsec = 0; tp = ts; }
                return pemu_callat(HOP_utimensat, AT_FDCWD, (const char*) a0, 1, (long) tp, 0, 0, 0); }
    case 133: return hs(HOP_mknodat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 261: { struct timespec ts[2], *tp = NULL;                                                          /* futimesat(dirfd, path, struct timeval[2]) */
                if (a2) { struct timeval*tv = (struct timeval*) a2; for (int i = 0; i < 2; i++) { ts[i].tv_sec = tv[i].tv_sec; ts[i].tv_nsec = tv[i].tv_usec * 1000L; } tp = ts; }
                return pemu_callat(HOP_utimensat, (long) a0, (const char*) a1, 1, (long) tp, 0, 0, 0); }
    case 253: return hs(HOP_inotify_init1, 0, 0, 0, 0, 0, 0);
    case 282: return hs(HOP_signalfd4, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 334: return -ENOSYS;                                                                                /* rseq: glibc and others fall back (seccomp is answered by pemu_sc_call) */
    /* io_uring: its OPENAT / STATX / UNLINKAT / RENAMEAT / MKDIRAT / SYMLINKAT / LINKAT / BIND / CONNECT operations carry guest
     * paths that the KERNEL resolves later, asynchronously, from the submission ring - pemu never sees them, so they would act on the
     * HOST file system, outside the rootfs (reproduced: open() read the rootfs file, an io_uring OPENAT of the same path read the host's).
     * With a rootfs there is nothing to translate them with: answer ENOSYS, as libuv, tokio, liburing
     * users and glibc-based programs expect when io_uring is unavailable (they fall back to plain syscalls).  Without a rootfs
     * (no -r / login) guest and host paths are the same thing, so the rings are passed through. */
    case 425: case 426: case 427:
        if (pemu_on) return PLOG_ENOSYS("io_uring would bypass the rootfs (paths are resolved by the kernel), declined inside a rootfs");
        if (n == 425) { long ur = hs(HOP_io_uring_setup, (long) a0, (long) a1, 0, 0, 0, 0);
                        if (ur >= 0) mt_barriers();                                                         /* the kernel now writes the rings behind our back */
                        return ur; }
        if (n == 426) return hs(HOP_io_uring_enter, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        return hs(HOP_io_uring_register, (long) a0, (long) a1, (long) a2, (long) a3, 0, 0);
    default:
        nr = pass_nr(n);
        if (nr >= 0) return hs(nr, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        plog_unsupported(n, a0, a1);
        if (g_trace) pemu_tr("pemu: unsupported syscall %llu\n", (unsigned long long) n);
        return -ENOSYS;
    }
}
static void do_syscall(CPU*c) {
    int noset = 0;
    u64 n = c->r[RAX];
    u64 ta0 = c->r[RDI], ta1 = c->r[RSI], ta2 = c->r[RDX];
    if (g_trace) pemu_tr("pemu: syscall %llu (%lld, %llx, %llx) ...\n", (unsigned long long) n, (long long) ta0, (unsigned long long) ta1, (unsigned long long) ta2);
    if (g_trace) {   /* show the path argument of path-taking syscalls */
        u64 pp = 0;
        switch (n) { case 2: case 4: case 6: case 21: case 59: case 80: case 83: case 84: case 87: case 89: case 90: case 92: pp = ta0; break;
                     case 257: case 258: case 262: case 263: case 267: case 268: case 269: case 322: pp = ta1; break; default: break; }
        if (pp > 0x10000 && pp < 0x7fffffffffffULL) pemu_tr("pemu:   path=\"%.200s\"\n", (const char*) (uintptr_t) pp);
    }
    plog_nr = n; plog_noted = 0;
    long r = gsys(c, &noset);
    plog_check_result(n, r); plog_nr = ~0UL;                     /* .pemu/pemu.log: an ENOSYS answer nobody logged yet */
    if (g_trace) pemu_tr("pemu: syscall %llu -> %ld\n", (unsigned long long) n, r);
    if (g_trace && n == 7 && r > 0 && ta0 > 0x10000) { const short*pf = (const short*) (uintptr_t) ta0; pemu_tr("pemu:   poll fd0=%d events=%#x revents=%#x nfds=%llu\n", *(const int*) pf, pf[2], pf[3], (unsigned long long) ta1); }
    if (g_trace && n == 53 && r == 0) { const int*sv = (const int*) (uintptr_t) c->r[10]; if (sv) pemu_tr("pemu:   socketpair -> fds %d %d\n", sv[0], sv[1]); }
    if (!noset) c->r[RAX] = (u64) r;
#ifdef HAVE_JIT
    if (g_jit_on) jit_post_syscall(c, n, r);
#endif
}


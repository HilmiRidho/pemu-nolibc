/* sizes, offsets and flag values that pemu shares with the kernel; one table, compiled with the system's headers and with host/abi/include (tests/integration/abi_layout.sh) */
#define P(t) (unsigned long) sizeof(t),
#define O(t, m) (unsigned long) offsetof(t, m),
#define V(x) (unsigned long) (x),
const unsigned long abi_values[] = {
    P(struct stat) O(struct stat, st_dev) O(struct stat, st_ino) O(struct stat, st_nlink) O(struct stat, st_mode) O(struct stat, st_uid) O(struct stat, st_gid) O(struct stat, st_rdev) O(struct stat, st_size)
    O(struct stat, st_blksize) O(struct stat, st_blocks) O(struct stat, st_atim) O(struct stat, st_mtim) O(struct stat, st_ctim)
    P(struct statx) O(struct statx, stx_ino) O(struct statx, stx_atime) O(struct statx, stx_rdev_major)
    P(struct timespec) P(struct timeval) P(struct itimerval) P(struct tm) P(struct tms) P(struct iovec) P(struct rlimit) P(struct rusage)
    P(struct utsname) P(struct sysinfo) O(struct sysinfo, mem_unit) O(struct sysinfo, totalhigh)
    P(struct dirent) O(struct dirent, d_reclen) O(struct dirent, d_type) O(struct dirent, d_name)
    P(struct epoll_event) O(struct epoll_event, data) P(struct pollfd)
    P(struct sockaddr_un) O(struct sockaddr_un, sun_path) P(struct msghdr) O(struct msghdr, msg_iov) O(struct msghdr, msg_control) O(struct msghdr, msg_flags) P(struct cmsghdr) P(struct ucred) P(struct mmsghdr)
    P(sigset_t) P(stack_t) O(stack_t, ss_flags) O(stack_t, ss_size)
    P(struct sigaction) O(struct sigaction, sa_mask) O(struct sigaction, sa_flags) O(struct sigaction, sa_restorer)
    P(siginfo_t) O(siginfo_t, si_code) V(offsetof(siginfo_t, si_addr)) V(offsetof(siginfo_t, si_pid)) V(offsetof(siginfo_t, si_status)) V(offsetof(siginfo_t, si_syscall)) V(offsetof(siginfo_t, si_arch))
    P(ucontext_t) O(ucontext_t, uc_stack) O(ucontext_t, uc_mcontext) O(ucontext_t, uc_sigmask) P(mcontext_t)
#if defined(__x86_64__)
    O(mcontext_t, gregs) O(mcontext_t, fpregs) V(REG_RIP) V(REG_RAX) V(REG_ERR) V(REG_EFL) V(REG_R8) V(REG_RSP)
#endif
    P(Elf64_Ehdr) P(Elf64_Phdr) P(Elf64_Dyn) P(Elf64_Sym) P(Elf64_Rela) P(Elf64_auxv_t)
    P(jmp_buf) P(sigjmp_buf)
    V(O_DIRECTORY) V(O_NOFOLLOW) V(O_DIRECT) V(O_CLOEXEC) V(O_SYNC) V(O_PATH) V(O_TMPFILE) V(O_NOATIME) V(O_DSYNC) V(O_ASYNC) V(O_APPEND) V(O_NONBLOCK)
    V(FE_TONEAREST) V(FE_UPWARD) V(FE_DOWNWARD) V(FE_TOWARDZERO) V(FE_INVALID) V(FE_DIVBYZERO) V(FE_OVERFLOW) V(FE_UNDERFLOW) V(FE_INEXACT)
    V(SA_SIGINFO) V(SA_RESTART) V(SA_NODEFER) V(SA_ONSTACK) V(SA_RESETHAND) V(SA_NOCLDSTOP) V(SIGSYS) V(SIGSTKFLT) V(SIGCHLD) V(SIGPWR) V(SIGWINCH) V(SIGUSR2)
    V(MAP_ANONYMOUS) V(MAP_NORESERVE) V(MAP_STACK) V(MAP_FIXED_NOREPLACE) V(MAP_POPULATE) V(MAP_GROWSDOWN) V(PROT_EXEC) V(MADV_DONTNEED)
    V(ENOTSUP) V(ETIMEDOUT) V(ESTALE) V(EOPNOTSUPP) V(EOVERFLOW) V(ELOOP) V(ENOSYS) V(EAGAIN)
    V(SYS_openat) V(SYS_getdents64) V(SYS_futex) V(SYS_statx) V(SYS_clone) V(SYS_rt_sigaction) V(SYS_mmap) V(SYS_exit_group) V(SYS_newfstatat) V(SYS_pidfd_open) V(SYS_openat2)
    V(AT_FDCWD) V(AT_SYMLINK_NOFOLLOW) V(AT_REMOVEDIR) V(AT_EMPTY_PATH) V(AT_HWCAP) V(AT_RANDOM) V(AT_SYSINFO_EHDR) V(AT_EXECFN)
    V(CLONE_VM) V(CLONE_THREAD) V(CLONE_SETTLS) V(CLONE_CHILD_CLEARTID) V(CLONE_PARENT_SETTID) V(S_IFMT) V(S_IFREG) V(S_ISUID)
    V(EM_X86_64) V(EM_AARCH64) V(R_X86_64_RELATIVE) V(R_AARCH64_RELATIVE) V(DT_GNU_HASH) V(PT_TLS) V(F_DUPFD_CLOEXEC) V(F_SETPIPE_SZ) V(SCM_CREDENTIALS) V(SO_PEERCRED) V(CLOCK_BOOTTIME)
    0
};

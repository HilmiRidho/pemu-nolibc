/* src/guest/x86_64/linux/sysnum.inc.h - x86_64 syscall numbers that pass straight through to the host. SPDX-License-Identifier: Apache-2.0 */
/* direct pass-through: guest x86_64 number -> host number (same arguments) */
/* less common pass-through syscalls; each guarded because headers differ per host */
#define P2(g, name) case g: return name;
static long pass_nr2(u64 n) {
    switch (n) {
#ifdef HOP_setxattr
    P2(188, HOP_setxattr) P2(189, HOP_lsetxattr) P2(190, HOP_fsetxattr) P2(191, HOP_getxattr) P2(192, HOP_lgetxattr)
    P2(193, HOP_fgetxattr) P2(194, HOP_listxattr) P2(195, HOP_llistxattr) P2(196, HOP_flistxattr)
    P2(197, HOP_removexattr) P2(198, HOP_lremovexattr) P2(199, HOP_fremovexattr)
#endif
    P2(140, HOP_getpriority) P2(141, HOP_setpriority) P2(142, HOP_sched_setparam) P2(143, HOP_sched_getparam)
    P2(144, HOP_sched_setscheduler) P2(145, HOP_sched_getscheduler) P2(146, HOP_sched_get_priority_max)
    P2(147, HOP_sched_get_priority_min) P2(148, HOP_sched_rr_get_interval) P2(149, HOP_mlock) P2(150, HOP_munlock)
    P2(151, HOP_mlockall) P2(152, HOP_munlockall) P2(135, HOP_personality) P2(165, HOP_mount) P2(166, HOP_umount2)
    P2(254, HOP_inotify_add_watch) P2(255, HOP_inotify_rm_watch)
    P2(275, HOP_splice) P2(276, HOP_tee) P2(277, HOP_sync_file_range) P2(278, HOP_vmsplice) P2(283, HOP_timerfd_create)
    P2(286, HOP_timerfd_settime) P2(287, HOP_timerfd_gettime) P2(289, HOP_signalfd4) P2(309, HOP_getcpu)
    P2(222, HOP_timer_create) P2(223, HOP_timer_settime) P2(224, HOP_timer_gettime) P2(225, HOP_timer_getoverrun)
    P2(226, HOP_timer_delete) P2(131 + 1000, HOP_sync)
#ifdef HOP_close_range
    P2(436, HOP_close_range)
#endif
#ifdef HOP_pidfd_open
    P2(434, HOP_pidfd_open) P2(424, HOP_pidfd_send_signal)
#endif
/* more pass-through syscalls (structures have the same layout on x86_64 and the generic aarch64 ABI) */
#ifdef HOP_shmget
    P2(29, HOP_shmget)
#endif
#ifdef HOP_shmat
    P2(30, HOP_shmat)
#endif
#ifdef HOP_shmdt
    P2(67, HOP_shmdt)
#endif
#ifdef HOP_semget
    P2(64, HOP_semget)
#endif
#ifdef HOP_semop
    P2(65, HOP_semop)
#endif
#ifdef HOP_semtimedop
    P2(220, HOP_semtimedop)
#endif
#ifdef HOP_msgget
    P2(68, HOP_msgget)
#endif
#ifdef HOP_msgsnd
    P2(69, HOP_msgsnd)
#endif
#ifdef HOP_msgrcv
    P2(70, HOP_msgrcv)
#endif
#ifdef HOP_rt_sigqueueinfo
    P2(129, HOP_rt_sigqueueinfo)
#endif
#ifdef HOP_rt_tgsigqueueinfo
    P2(297, HOP_rt_tgsigqueueinfo)
#endif
#ifdef HOP_readahead
    P2(187, HOP_readahead)
#endif
#ifdef HOP_ioprio_set
    P2(251, HOP_ioprio_set)
#endif
#ifdef HOP_ioprio_get
    P2(252, HOP_ioprio_get)
#endif
#ifdef HOP_unshare
    P2(272, HOP_unshare)                          /* only flags without CLONE_NEW* get here (src/proot/deny.inc.h) */
#endif
#ifdef HOP_sched_setattr
    P2(314, HOP_sched_setattr)
#endif
#ifdef HOP_sched_getattr
    P2(315, HOP_sched_getattr)
#endif
#ifdef HOP_fanotify_init
    P2(300, HOP_fanotify_init)
#endif
#ifdef HOP_fanotify_mark
    P2(301, HOP_fanotify_mark)
#endif
#ifdef HOP_name_to_handle_at
    P2(303, HOP_name_to_handle_at)                /* without a rootfs only; with one the policy translates the path (src/proot/deny.inc.h) */
#endif
#ifdef HOP_membarrier
    P2(324, HOP_membarrier)
#endif
#ifdef HOP_mlock2
    P2(325, HOP_mlock2)
#endif
#ifdef HOP_preadv2
    P2(327, HOP_preadv2)
#endif
#ifdef HOP_pwritev2
    P2(328, HOP_pwritev2)
#endif
#ifdef HOP_io_setup
    P2(206, HOP_io_setup)
#endif
#ifdef HOP_io_destroy
    P2(207, HOP_io_destroy)
#endif
#ifdef HOP_io_getevents
    P2(208, HOP_io_getevents)
#endif
#ifdef HOP_io_submit
    P2(209, HOP_io_submit)
#endif
#ifdef HOP_io_cancel
    P2(210, HOP_io_cancel)
#endif
#ifdef HOP_mq_open
    P2(240, HOP_mq_open)
#endif
#ifdef HOP_mq_unlink
    P2(241, HOP_mq_unlink)
#endif
#ifdef HOP_mq_timedsend
    P2(242, HOP_mq_timedsend)
#endif
#ifdef HOP_mq_timedreceive
    P2(243, HOP_mq_timedreceive)
#endif
#ifdef HOP_mq_notify
    P2(244, HOP_mq_notify)
#endif
#ifdef HOP_mq_getsetattr
    P2(245, HOP_mq_getsetattr)
#endif
#ifdef HOP_mbind
    P2(237, HOP_mbind)
#endif
#ifdef HOP_set_mempolicy
    P2(238, HOP_set_mempolicy)
#endif
#ifdef HOP_get_mempolicy
    P2(239, HOP_get_mempolicy)
#endif
#ifdef HOP_faccessat2
    P2(439, HOP_faccessat2)
#endif
#ifdef HOP_epoll_pwait2
    P2(441, HOP_epoll_pwait2)
#endif
#ifdef HOP_setfsuid
    P2(122, HOP_setfsuid)
#endif
#ifdef HOP_setfsgid
    P2(123, HOP_setfsgid)
#endif
#ifdef HOP_io_pgetevents
    P2(333, HOP_io_pgetevents)
#endif
#ifdef HOP_memfd_secret
    P2(447, HOP_memfd_secret)
#endif
#ifdef HOP_process_mrelease
    P2(448, HOP_process_mrelease)
#endif
#ifdef HOP_futex_waitv
    P2(449, HOP_futex_waitv)
#endif
#ifdef HOP_set_mempolicy_home_node
    P2(450, HOP_set_mempolicy_home_node)
#endif
#ifdef HOP_cachestat
    P2(451, HOP_cachestat)
#endif
#ifdef HOP_fchmodat2
    P2(452, HOP_fchmodat2)
#endif
#ifdef HOP_futex_wake
    P2(454, HOP_futex_wake)
#endif
#ifdef HOP_futex_wait
    P2(455, HOP_futex_wait)
#endif
#ifdef HOP_futex_requeue
    P2(456, HOP_futex_requeue)
#endif
#ifdef HOP_lsm_get_self_attr
    P2(459, HOP_lsm_get_self_attr)
#endif
#ifdef HOP_lsm_list_modules
    P2(461, HOP_lsm_list_modules)
#endif
    default: return -1;
    }
}

/* direct pass-through: see pass_nr2 for the rest */
static long pass_nr(u64 n) {
    switch (n) {
    case 0: return HOP_read; case 1: return HOP_write; case 3: return HOP_close; case 8: return HOP_lseek;
    case 9: return HOP_mmap; case 10: return HOP_mprotect; case 11: return HOP_munmap; case 16: return HOP_ioctl;
    case 17: return HOP_pread64; case 18: return HOP_pwrite64; case 19: return HOP_readv; case 20: return HOP_writev;
    case 24: return HOP_sched_yield; case 25: return HOP_mremap; case 26: return HOP_msync; case 27: return HOP_mincore;
    case 28: return HOP_madvise; case 32: return HOP_dup; case 35: return HOP_nanosleep; case 36: return HOP_getitimer;
    case 38: return HOP_setitimer; case 39: return HOP_getpid; case 40: return HOP_sendfile; case 41: return HOP_socket;
    case 42: return HOP_connect; case 43: return HOP_accept; case 44: return HOP_sendto; case 45: return HOP_recvfrom;
    case 46: return HOP_sendmsg; case 47: return HOP_recvmsg; case 48: return HOP_shutdown; case 49: return HOP_bind;
    case 50: return HOP_listen; case 51: return HOP_getsockname; case 52: return HOP_getpeername;
    case 53: return HOP_socketpair; case 54: return HOP_setsockopt; case 55: return HOP_getsockopt;
    case 61: return HOP_wait4; case 62: return HOP_kill; case 73: return HOP_flock; case 74: return HOP_fsync;
    case 75: return HOP_fdatasync; case 76: return HOP_truncate; case 77: return HOP_ftruncate; case 79: return HOP_getcwd;
    case 80: return HOP_chdir; case 81: return HOP_fchdir; case 91: return HOP_fchmod; case 93: return HOP_fchown;
    case 95: return HOP_umask; case 96: return HOP_gettimeofday; case 97: return HOP_getrlimit; case 98: return HOP_getrusage;
    case 99: return HOP_sysinfo; case 100: return HOP_times; case 102: return HOP_getuid; case 104: return HOP_getgid;
    case 105: return HOP_setuid; case 106: return HOP_setgid; case 107: return HOP_geteuid; case 108: return HOP_getegid;
    case 109: return HOP_setpgid; case 110: return HOP_getppid; case 112: return HOP_setsid; case 113: return HOP_setreuid;
    case 114: return HOP_setregid; case 115: return HOP_getgroups; case 116: return HOP_setgroups;
    case 117: return HOP_setresuid; case 118: return HOP_getresuid; case 119: return HOP_setresgid;
    case 120: return HOP_getresgid; case 121: return HOP_getpgid; case 124: return HOP_getsid; case 125: return HOP_capget;
    case 126: return HOP_capset; case 127: return HOP_rt_sigpending; case 128: return HOP_rt_sigtimedwait;
    case 130: return HOP_rt_sigsuspend; case 137: return HOP_statfs; case 138: return HOP_fstatfs; case 157: return HOP_prctl;
    case 160: return HOP_setrlimit; case 161: return HOP_chroot; case 162: return HOP_sync; case 186: return HOP_gettid;
    case 200: return HOP_tkill; case 202: return HOP_futex; case 203: return HOP_sched_setaffinity;
    case 204: return HOP_sched_getaffinity; case 217: return HOP_getdents64; case 218: return HOP_set_tid_address;
    case 221: return HOP_fadvise64; case 228: return HOP_clock_gettime; case 229: return HOP_clock_getres;
    case 230: return HOP_clock_nanosleep; case 233: return HOP_epoll_ctl; case 234: return HOP_tgkill;
    case 258: return HOP_mkdirat; case 259: return HOP_mknodat; case 260: return HOP_fchownat; case 263: return HOP_unlinkat;
    case 264: return HOP_renameat; case 265: return HOP_linkat; case 266: return HOP_symlinkat;
    case 267: return HOP_readlinkat; case 268: return HOP_fchmodat; case 269: return HOP_faccessat;
    case 270: return HOP_pselect6; case 271: return HOP_ppoll; case 273: return HOP_set_robust_list;
    case 274: return HOP_get_robust_list; case 280: return HOP_utimensat; case 285: return HOP_fallocate;
    case 288: return HOP_accept4; case 290: return HOP_eventfd2; case 291: return HOP_epoll_create1;
    case 293: return HOP_pipe2; case 294: return HOP_inotify_init1; case 302: return HOP_prlimit64;
    case 316: return HOP_renameat2; case 318: return HOP_getrandom; case 319: return HOP_memfd_create;
    case 332: return HOP_statx; case 247: return HOP_waitid; case 306: return HOP_syncfs; case 326: return HOP_copy_file_range;
    case 295: return HOP_preadv; case 296: return HOP_pwritev; case 299: return HOP_recvmmsg; case 307: return HOP_sendmmsg;
    case 5: return -2;
    default: return pass_nr2(n);
    }
}


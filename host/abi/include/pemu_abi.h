/* host/abi/include/pemu_abi.h - the ABI of the host kernel as pemu needs it, with the libc names.  SPDX-License-Identifier: Apache-2.0
 *
 * The build (build.sh; there is only this one) is compiled with -nostdinc and only this directory on the include path: it reads NO header of
 * the system's libc (glibc, musl, bionic, ...) and none of the kernel's either.  Every other header in this directory (stdio.h, signal.h,
 * sys/stat.h ...) only includes this file.  What is here is Linux kernel ABI for x86_64 and aarch64 (struct layouts, numbers, flags), written with
 * the names the code already uses, so no caller changed.  The functions are declared here and defined in host/rt/.
 *
 * x86_64 and aarch64 differ in: struct stat, the open flags O_DIRECTORY / O_NOFOLLOW / O_DIRECT, ucontext / mcontext, the fenv constants, epoll_event
 * packing, nlink_t, setjmp.  Everything else (signal numbers, errno, mmap flags, AT_*, ELF) is the same. */
#ifndef PEMU_ABI_H
#define PEMU_ABI_H
#if !defined(__x86_64__) && !defined(__aarch64__)
#error "pemu_abi.h: x86_64 and aarch64 only"
#endif
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include "pemu_errno.h"
#include "pemu_nr.h"

/* ---------------------------------------------------------------- types */
typedef long ssize_t; typedef int pid_t; typedef unsigned uid_t; typedef unsigned gid_t; typedef unsigned mode_t; typedef unsigned id_t;
typedef long off_t; typedef long off64_t; typedef long long loff_t; typedef long time_t; typedef long suseconds_t; typedef long clock_t; typedef unsigned useconds_t;
typedef long blksize_t; typedef long blkcnt_t; typedef unsigned long dev_t; typedef unsigned long ino_t; typedef int clockid_t; typedef int key_t;
typedef unsigned socklen_t; typedef unsigned short sa_family_t; typedef unsigned long rlim_t; typedef int __rlimit_resource_t;
typedef unsigned char u_char; typedef unsigned short u_short; typedef unsigned u_int; typedef unsigned long u_long;
#if defined(__x86_64__)
typedef unsigned long nlink_t;
#else
typedef unsigned nlink_t;
#endif
typedef unsigned long fsblkcnt_t, fsfilcnt_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };
struct itimerval { struct timeval it_interval, it_value; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char *tm_zone; };
struct tms { clock_t tms_utime, tms_stime, tms_cutime, tms_cstime; };
struct iovec { void *iov_base; size_t iov_len; };
struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65]; };
struct sysinfo { long uptime; unsigned long loads[3], totalram, freeram, sharedram, bufferram, totalswap, freeswap; unsigned short procs, pad;
                 unsigned long totalhigh, freehigh; unsigned mem_unit; char _f[20 - 2 * sizeof(long) - sizeof(int)]; };
struct rlimit { rlim_t rlim_cur, rlim_max; };
struct rlimit64 { unsigned long long rlim_cur, rlim_max; };
struct rusage { struct timeval ru_utime, ru_stime; long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw; };
typedef struct { unsigned long __bits[16]; } cpu_set_t;
struct pollfd { int fd; short events, revents; };
#define POLLIN 1
#define POLLPRI 2
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32

/* struct stat: the kernel's layout (x86_64: asm/stat.h; aarch64: asm-generic/stat.h) */
#if defined(__x86_64__)
struct stat { dev_t st_dev; ino_t st_ino; nlink_t st_nlink; mode_t st_mode; uid_t st_uid; gid_t st_gid; int __pad0; dev_t st_rdev; off_t st_size; blksize_t st_blksize; blkcnt_t st_blocks;
              struct timespec st_atim, st_mtim, st_ctim; long __unused[3]; };
#else
struct stat { dev_t st_dev; ino_t st_ino; mode_t st_mode; nlink_t st_nlink; uid_t st_uid; gid_t st_gid; dev_t st_rdev; unsigned long __pad1; off_t st_size; int st_blksize; int __pad2;
              long st_blocks; struct timespec st_atim, st_mtim, st_ctim; unsigned __unused4, __unused5; };
#endif
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
#define stat64 stat
#define S_IFMT 0170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 060000
#define S_IFDIR 040000
#define S_IFCHR 020000
#define S_IFIFO 010000
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRWXU 0700
#define S_IRWXG 070
#define S_IRWXO 07
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
struct statx_timestamp { int64_t tv_sec; uint32_t tv_nsec; int32_t __reserved; };
struct statx { uint32_t stx_mask, stx_blksize; uint64_t stx_attributes; uint32_t stx_nlink, stx_uid, stx_gid; uint16_t stx_mode, __spare0[1]; uint64_t stx_ino, stx_size, stx_blocks, stx_attributes_mask;
               struct statx_timestamp stx_atime, stx_btime, stx_ctime, stx_mtime; uint32_t stx_rdev_major, stx_rdev_minor, stx_dev_major, stx_dev_minor; uint64_t __spare2[14]; };
/* the kernel's linux_dirent64 is the layout of struct dirent */
struct dirent { uint64_t d_ino; int64_t d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
#define dirent64 dirent
typedef struct __rt_DIR DIR;
typedef struct __rt_FILE FILE;

/* ---------------------------------------------------------------- sockets */
#define AF_UNIX 1
#define AF_LOCAL 1
#define PF_UNIX 1
#define SOL_SOCKET 1
#define SO_PEERCRED 17
#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 04000
#define SOCK_CLOEXEC 02000000
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_un { sa_family_t sun_family; char sun_path[108]; };
struct msghdr { void *msg_name; socklen_t msg_namelen; struct iovec *msg_iov; size_t msg_iovlen; void *msg_control; size_t msg_controllen; int msg_flags; };
struct cmsghdr { size_t cmsg_len; int cmsg_level; int cmsg_type; };
struct mmsghdr { struct msghdr msg_hdr; unsigned msg_len; };
struct ucred { pid_t pid; uid_t uid; gid_t gid; };
#define CMSG_ALIGN(l) (((l) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_DATA(c) ((unsigned char *) ((struct cmsghdr *) (c) + 1))
#define CMSG_LEN(l) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (l))
#define CMSG_SPACE(l) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(l))
#define CMSG_FIRSTHDR(m) ((m)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *) (m)->msg_control : (struct cmsghdr *) 0)
#define CMSG_NXTHDR(m, c) ((c)->cmsg_len < sizeof(struct cmsghdr) || (unsigned char *) (c) + CMSG_ALIGN((c)->cmsg_len) + sizeof(struct cmsghdr) > (unsigned char *) (m)->msg_control + (m)->msg_controllen ? (struct cmsghdr *) 0 : (struct cmsghdr *) ((unsigned char *) (c) + CMSG_ALIGN((c)->cmsg_len)))
typedef union epoll_data { void *ptr; int fd; uint32_t u32; uint64_t u64; } epoll_data_t;
#if defined(__x86_64__)
struct epoll_event { uint32_t events; epoll_data_t data; } __attribute__((packed));
#else
struct epoll_event { uint32_t events; epoll_data_t data; };
#endif
#define EPOLLIN 1
#define EPOLLOUT 4
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

/* ---------------------------------------------------------------- files, memory, processes */
#if defined(__x86_64__)
#define O_DIRECT 040000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#else
#define O_DIRECTORY 040000
#define O_NOFOLLOW 0100000
#define O_DIRECT 0200000
#endif
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_ASYNC 020000
#define O_LARGEFILE 0
#define O_NOATIME 01000000
#define O_CLOEXEC 02000000
#define O_SYNC 04010000
#define O_PATH 010000000
#define O_TMPFILE (020000000 | O_DIRECTORY)
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 1030
#define F_SETPIPE_SZ 1031
#define FD_CLOEXEC 1
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_EACCESS 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EMPTY_PATH 0x1000
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_GROWSDOWN 0x100
#define MAP_NORESERVE 0x4000
#define MAP_POPULATE 0x8000
#define MAP_STACK 0x20000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *) -1)
#define MADV_NORMAL 0
#define MADV_DONTNEED 4
#define MS_ASYNC 1
#define MS_SYNC 4
#define MS_INVALIDATE 2
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2
#define WNOHANG 1
#define WUNTRACED 2
#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WIFEXITED(s) (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (((signed char) (((s) & 0x7f) + 1) >> 1) > 0)
#define WIFSTOPPED(s) (((s) & 0xff) == 0x7f)
#define PR_SET_PDEATHSIG 1
#define PR_SET_CHILD_SUBREAPER 36
#define RLIMIT_NOFILE 7
#define RLIMIT_STACK 3
#define RLIM_INFINITY (~0UL)
#define CLONE_VM 0x100
#define CLONE_FS 0x200
#define CLONE_FILES 0x400
#define CLONE_SIGHAND 0x800
#define CLONE_THREAD 0x10000
#define CLONE_SYSVSEM 0x40000
#define CLONE_SETTLS 0x80000
#define CLONE_PARENT_SETTID 0x100000
#define CLONE_CHILD_CLEARTID 0x200000
struct clone_args { uint64_t flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls, set_tid, set_tid_size, cgroup; };
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME 7
#define ITIMER_REAL 0
#define _SC_ARG_MAX 0
#define _SC_CLK_TCK 2
#define _SC_OPEN_MAX 4
#define _SC_PAGESIZE 30
#define _SC_NPROCESSORS_CONF 83
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PHYS_PAGES 85
#define GRND_NONBLOCK 1

/* ---------------------------------------------------------------- ELF */
typedef uint64_t Elf64_Addr, Elf64_Off, Elf64_Xword; typedef int64_t Elf64_Sxword; typedef uint32_t Elf64_Word; typedef uint16_t Elf64_Half; typedef int32_t Elf64_Sword;
typedef struct { unsigned char e_ident[16]; Elf64_Half e_type, e_machine; Elf64_Word e_version; Elf64_Addr e_entry; Elf64_Off e_phoff, e_shoff; Elf64_Word e_flags;
                 Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } Elf64_Ehdr;
typedef struct { Elf64_Word p_type, p_flags; Elf64_Off p_offset; Elf64_Addr p_vaddr, p_paddr; Elf64_Xword p_filesz, p_memsz, p_align; } Elf64_Phdr;
typedef struct { Elf64_Sxword d_tag; union { Elf64_Xword d_val; Elf64_Addr d_ptr; } d_un; } Elf64_Dyn;
typedef struct { Elf64_Word st_name; unsigned char st_info, st_other; Elf64_Half st_shndx; Elf64_Addr st_value; Elf64_Xword st_size; } Elf64_Sym;
typedef struct { Elf64_Addr r_offset; Elf64_Xword r_info; Elf64_Sxword r_addend; } Elf64_Rela;
typedef struct { uint64_t a_type; union { uint64_t a_val; } a_un; } Elf64_auxv_t;
#define EI_CLASS 4
#define ELFCLASS64 2
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define EM_AARCH64 183
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_PHDR 6
#define PT_TLS 7
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define DT_NULL 0
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_GNU_HASH 0x6ffffef5
#define STT_FUNC 2
#define ELF64_R_TYPE(i) ((i) & 0xffffffffUL)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define R_X86_64_RELATIVE 8
#define R_AARCH64_RELATIVE 1027
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_HWCAP2 26
#define AT_EXECFN 31
#define AT_SYSINFO_EHDR 33
extern char **__environ;
extern char **environ;
extern Elf64_Dyn _DYNAMIC[];

/* ---------------------------------------------------------------- signals */
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31
#define NSIG 65
#define SA_NOCLDSTOP 1
#define SA_NOCLDWAIT 2
#define SA_SIGINFO 4
#define SA_RESTORER 0x04000000
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIG_DFL ((void (*)(int)) 0)
#define SIG_IGN ((void (*)(int)) 1)
#define SIG_ERR ((void (*)(int)) -1)
#define SS_ONSTACK 1
#define SS_DISABLE 2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192
#define SI_USER 0
#define SI_KERNEL 0x80
#define SI_QUEUE (-1)
#define SI_TKILL (-6)
#define TRAP_BRKPT 1
#define TRAP_TRACE 2
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
#define BUS_ADRALN 1
#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define FPE_FLTDIV 3
#define FPE_FLTOVF 4
#define FPE_FLTUND 5
#define FPE_FLTRES 6
#define FPE_FLTINV 7
#define FPE_FLTSUB 8
#define ILL_ILLOPC 1
#define SYS_SECCOMP 1
typedef struct { unsigned long __bits[16]; } sigset_t;
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
typedef struct {
    int si_signo, si_errno, si_code, __pad0;
    union {
        int _pad[28];
        struct { pid_t si_pid; uid_t si_uid; } _kill;
        struct { int si_tid, si_overrun; } _timer;
        struct { pid_t si_pid; uid_t si_uid; int si_status; clock_t si_utime, si_stime; } _sigchld;
        struct { void *si_addr; short si_addr_lsb; } _sigfault;
        struct { void *_call_addr; int _syscall; unsigned _arch; } _sigsys;
    } _sifields;
} siginfo_t;
#define si_pid _sifields._kill.si_pid
#define si_uid _sifields._kill.si_uid
#define si_status _sifields._sigchld.si_status
#define si_addr _sifields._sigfault.si_addr
#define si_call_addr _sifields._sigsys._call_addr
#define si_syscall _sifields._sigsys._syscall
#define si_arch _sifields._sigsys._arch
struct sigaction {
    union { void (*sa_handler)(int); void (*sa_sigaction)(int, siginfo_t *, void *); } __sa_h;
    sigset_t sa_mask; int sa_flags; void (*sa_restorer)(void);
};
#define sa_handler __sa_h.sa_handler
#define sa_sigaction __sa_h.sa_sigaction
#if defined(__x86_64__)
enum { REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_RDI, REG_RSI, REG_RBP, REG_RBX, REG_RDX, REG_RAX, REG_RCX, REG_RSP, REG_RIP, REG_EFL, REG_CSGSFS, REG_ERR, REG_TRAPNO, REG_OLDMASK, REG_CR2 };
typedef struct { unsigned long gregs[23]; void *fpregs; unsigned long __reserved1[8]; } mcontext_t;
typedef struct __ucontext { unsigned long uc_flags; struct __ucontext *uc_link; stack_t uc_stack; mcontext_t uc_mcontext; sigset_t uc_sigmask; unsigned char __fpregs_mem[512]; unsigned long long __ssp[4]; } ucontext_t;
#else
struct sigcontext { unsigned long fault_address; unsigned long regs[31]; unsigned long sp, pc, pstate; unsigned char __reserved[4096] __attribute__((aligned(16))); };
typedef struct sigcontext mcontext_t;
typedef struct __ucontext { unsigned long uc_flags; struct __ucontext *uc_link; stack_t uc_stack; sigset_t uc_sigmask; mcontext_t uc_mcontext __attribute__((aligned(16))); } ucontext_t;
#endif
/* setjmp: own register layout (rt_core.h), the size is glibc's so a sigjmp_buf of either fits */
typedef struct { long __jb[8]; int __mask_was_saved; sigset_t __saved_mask; } sigjmp_buf[1];
typedef sigjmp_buf jmp_buf;
int __sigsetjmp(sigjmp_buf, int) __attribute__((returns_twice));
int setjmp(jmp_buf) __attribute__((returns_twice));
int _setjmp(jmp_buf) __attribute__((returns_twice));
#define sigsetjmp(e, s) __sigsetjmp(e, s)
void longjmp(jmp_buf, int) __attribute__((noreturn));
void siglongjmp(sigjmp_buf, int) __attribute__((noreturn));

/* ---------------------------------------------------------------- threads (the first int of a mutex is the futex word) */
typedef unsigned long pthread_t;
typedef struct { size_t __a[7]; } pthread_attr_t;
typedef struct { int __lock; int __pad[9]; } pthread_mutex_t;
typedef struct { int __k; } pthread_mutexattr_t;
typedef int pthread_once_t;
#define PTHREAD_MUTEX_INITIALIZER { 0, { 0 } }
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

/* ---------------------------------------------------------------- stdio, stdlib, errno */
#define EOF (-1)
#define BUFSIZ 8192
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define FILENAME_MAX 4096
#define RAND_MAX 2147483647
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
extern FILE *stdin, *stdout, *stderr;
int *__errno_location(void);
#define errno (*__errno_location())
#define MB_CUR_MAX 1

/* ctype: C locale, through the tables of rt_mem.h (the glibc scheme, so the same macros) */
enum { _ISupper = 0x100, _ISlower = 0x200, _ISalpha = 0x400, _ISdigit = 0x800, _ISxdigit = 0x1000, _ISspace = 0x2000, _ISprint = 0x4000, _ISgraph = 0x8000,
       _ISblank = 0x1, _IScntrl = 0x2, _ISpunct = 0x4, _ISalnum = 0x8 };
const unsigned short **__ctype_b_loc(void);
const int **__ctype_tolower_loc(void);
const int **__ctype_toupper_loc(void);
#define __isctype(c, t) ((*__ctype_b_loc())[(int) (c)] & (unsigned short) (t))
#define isalnum(c) __isctype(c, _ISalnum)
#define isalpha(c) __isctype(c, _ISalpha)
#define isblank(c) __isctype(c, _ISblank)
#define iscntrl(c) __isctype(c, _IScntrl)
#define isdigit(c) __isctype(c, _ISdigit)
#define isgraph(c) __isctype(c, _ISgraph)
#define islower(c) __isctype(c, _ISlower)
#define isprint(c) __isctype(c, _ISprint)
#define ispunct(c) __isctype(c, _ISpunct)
#define isspace(c) __isctype(c, _ISspace)
#define isupper(c) __isctype(c, _ISupper)
#define isxdigit(c) __isctype(c, _ISxdigit)
#define tolower(c) ((*__ctype_tolower_loc())[(int) (c)])
#define toupper(c) ((*__ctype_toupper_loc())[(int) (c)])

/* ---------------------------------------------------------------- math, fenv */
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf_sign(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define M_PI 3.14159265358979323846
#define M_LN2 0.69314718055994530942
#if defined(__x86_64__)
#define FE_INVALID 0x01
#define FE_DIVBYZERO 0x04
#define FE_OVERFLOW 0x08
#define FE_UNDERFLOW 0x10
#define FE_INEXACT 0x20
#define FE_ALL_EXCEPT 0x3d
#define FE_TONEAREST 0
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00
#else
#define FE_INVALID 1
#define FE_DIVBYZERO 2
#define FE_OVERFLOW 4
#define FE_UNDERFLOW 8
#define FE_INEXACT 16
#define FE_ALL_EXCEPT 0x1f
#define FE_TONEAREST 0
#define FE_UPWARD 0x400000
#define FE_DOWNWARD 0x800000
#define FE_TOWARDZERO 0xc00000
#endif
#define offsetof(t, m) __builtin_offsetof(t, m)
#include "pemu_proto.h"
#endif /* PEMU_ABI_H */

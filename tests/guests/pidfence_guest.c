/* tests/guests/pidfence_guest.c - x86_64 guest for tests/integration/pidfence.sh: calls that name a PID must only reach the guest's own process tree.
 * usage: guest VICTIM_PID [probe]   VICTIM_PID is a HOST process the guest did not start.  "probe": only kill(VICTIM, 0). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/syscall.h>
static void show(const char *what, long r) { printf("%s: %s\n", what, r >= 0 ? "ok" : strerror(errno)); fflush(stdout); }
#define SC(what, ...) do { long r_ = syscall(__VA_ARGS__); show(what, r_); } while (0)
static volatile int usr1;
static void on_usr1(int s) { (void) s; usr1++; }
static void child_usr(int s) { _exit(s == SIGUSR1 ? 7 : 8); }
static pid_t spawn_waiter(int ignore_usr1) {           /* a child that exits 7 on SIGUSR1 (or ignores it) and 8 on SIGUSR2 */
    pid_t p = fork();
    if (p == 0) { signal(SIGUSR1, ignore_usr1 ? SIG_IGN : child_usr); signal(SIGUSR2, child_usr); for (;;) pause(); }
    return p;
}
int main(int argc, char **argv) {
    pid_t v = argc > 1 ? (pid_t) atoi(argv[1]) : 0;
    if (argc > 2 && !strcmp(argv[2], "reexec")) {          /* after an execve inside the guest, with PEMU_PID_FENCE=0 set by the guest itself */
        SC("reexec kill(victim,0)", SYS_kill, v, 0); SC("reexec getpgid(victim)", SYS_getpgid, v); return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "probe")) { SC("probe kill(victim,0)", SYS_kill, v, 0); return 0; }
    /* 1. a process the guest did not start */
    SC("kill(victim,0)", SYS_kill, v, 0);
    SC("tgkill(victim)", SYS_tgkill, v, v, 0);
    SC("pidfd_open(victim)", SYS_pidfd_open, v, 0);
    { cpu_set_t m; SC("sched_getaffinity(victim)", SYS_sched_getaffinity, v, sizeof m, &m); }
    SC("getpgid(victim)", SYS_getpgid, v);
    SC("getsid(victim)", SYS_getsid, v);
    SC("getpriority(victim)", SYS_getpriority, PRIO_PROCESS, v);
    SC("ioprio_get(victim)", SYS_ioprio_get, 1, v);
    { struct { unsigned v; int pid; } h = { 0x20080522, v }; unsigned char d[24]; SC("capget(victim)", SYS_capget, &h, d); }
    SC("ioprio_set(user)", SYS_ioprio_set, 3, 0, 0);
    /* 2. the guest's own processes still work */
    signal(SIGUSR1, on_usr1);
    pid_t c = spawn_waiter(0), c2 = spawn_waiter(1);
    usleep(200000);
    SC("kill(self,0)", SYS_kill, getpid(), 0);
    SC("kill(child,0)", SYS_kill, c, 0);
    SC("tgkill(self)", SYS_tgkill, getpid(), syscall(SYS_gettid), 0);
    { cpu_set_t m; SC("sched_getaffinity(child)", SYS_sched_getaffinity, c, sizeof m, &m); }
    SC("getpgid(child)", SYS_getpgid, c);
    SC("kill(-1,0)", SYS_kill, -1, 0);
    /* 3. group and broadcast forms reach the tree and nothing else */
    SC("kill(0,USR1)", SYS_kill, 0, SIGUSR1);          /* own group: self (handler), c, c2 ... and, on the host, the victim: it must NOT get it */
    int st = 0; waitpid(c, &st, 0);
    printf("child got USR1: %s\n", WIFEXITED(st) && WEXITSTATUS(st) == 7 ? "yes" : "no"); fflush(stdout);
    printf("self got USR1: %s\n", usr1 == 1 ? "yes" : "no"); fflush(stdout);
    SC("kill(-1,USR2)", SYS_kill, -1, SIGUSR2);         /* c2 is still alive: it must get it, the victim must not */
    waitpid(c2, &st, 0);
    printf("child got USR2: %s\n", WIFEXITED(st) && WEXITSTATUS(st) == 8 ? "yes" : "no"); fflush(stdout);
    errno = 0; long r = syscall(SYS_kill, -1, 0);       /* nobody left but ourselves: kill(-1) leaves the caller out */
    printf("kill(-1,0) alone: %s\n", r == 0 ? "ok" : strerror(errno)); fflush(stdout);
    SC("setpriority(user)", SYS_setpriority, PRIO_USER, 0, 5);
    errno = 0; r = syscall(SYS_getpriority, PRIO_USER, 0);
    printf("getpriority(user): %s\n", r > 0 ? "ok" : strerror(errno)); fflush(stdout);
    /* 4. POSIX message queue names are per rootfs */
    long q = syscall(SYS_mq_open, "pemu_fence_q", O_CREAT | O_RDWR, 0600, NULL);
    show("mq_open", q);
    SC("mq_unlink", SYS_mq_unlink, "pemu_fence_q");
    errno = 0; r = syscall(SYS_mq_unlink, "pemu_fence_q");
    printf("mq_unlink again: %s\n", r == 0 ? "ok" : strerror(errno)); fflush(stdout);
    setenv("PEMU_PID_FENCE", "0", 1);                   /* the guest controls its own environment: this must not switch the fence off */
    execl("/g", "g", argv[1], "reexec", (char*) NULL);
    show("execve", -1);
    return 0;
}

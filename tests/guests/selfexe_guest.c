/* tests/guests/selfexe_guest.c - x86_64 guest for tests/integration/selfexe.sh: the guest's own identity in /proc, and prctl(PR_SET_SECCOMP).
 * Electron / Chromium locate their resources through readlink("/proc/self/exe") and start every child process (renderer, gpu,
 * utility) as execve("/proc/self/exe", ...). usage: guest            -> runs the checks
 *                                                    guest --child ARG -> what the exec'd child prints */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
int main(int argc, char **argv, char **envp) {
    if (argc > 2 && !strcmp(argv[1], "--child")) { printf("child ok %s\n", argv[2]); return 0; }
    char b[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n > 0) b[n] = 0;
    printf("exe: %s\n", n > 0 ? b : strerror(errno));
    char pid[64]; snprintf(pid, sizeof pid, "/proc/%d/exe", (int) getpid());
    n = readlink(pid, b, sizeof b - 1); if (n > 0) b[n] = 0;
    printf("exe by pid: %s\n", n > 0 ? b : strerror(errno));
    /* the file behind /proc/self/exe must be this program: an ELF header, not the emulator */
    struct stat st1, st2; int s1 = stat("/proc/self/exe", &st1), s2 = stat(argv[0], &st2);
    printf("stat same file: %s\n", s1 == 0 && s2 == 0 && st1.st_ino == st2.st_ino && st1.st_size == st2.st_size ? "yes" : "no");
    int fd = open("/proc/self/exe", O_RDONLY); char magic[4] = {0}; ssize_t m = fd >= 0 ? read(fd, magic, 4) : -1; if (fd >= 0) close(fd);
    printf("open exe: %s\n", m == 4 && !memcmp(magic, "\177ELF", 4) ? "elf" : "not elf");
    /* cmdline: the guest's argv, NUL separated */
    char cl[512] = {0}; fd = open("/proc/self/cmdline", O_RDONLY); ssize_t k = fd >= 0 ? read(fd, cl, sizeof cl - 1) : -1; if (fd >= 0) close(fd);
    size_t want = 0; for (int i = 0; i < argc; i++) want += strlen(argv[i]) + 1;
    printf("cmdline: %s\n", k == (ssize_t) want && !strcmp(cl, argv[0]) ? "ok" : "wrong");
    /* a child started as /proc/self/exe is this program again */
    pid_t c = fork();
    if (c == 0) { char *av[] = { argv[0], "--child", "x", NULL }; execve("/proc/self/exe", av, envp); printf("exec failed: %s\n", strerror(errno)); _exit(9); }
    int stt = 0; waitpid(c, &stt, 0); printf("exec child status: %d\n", WIFEXITED(stt) ? WEXITSTATUS(stt) : -1);
    fflush(stdout);
    /* seccomp: a guest filter must not reach the host process (no_new_privs is not set: EACCES, as on Linux) */
    errno = 0; long r = prctl(PR_SET_SECCOMP, 2, NULL);
    printf("PR_SET_SECCOMP: %s\n", r == 0 ? "accepted" : strerror(errno));
    printf("PR_GET_SECCOMP: %d\n", (int) prctl(PR_GET_SECCOMP));
    errno = 0; r = syscall(SYS_seccomp, 1, 0, NULL);
    printf("seccomp(): %s\n", r == 0 ? "accepted" : strerror(errno));
    return 0;
}

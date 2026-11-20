/* guest for tests/integration/suid.sh: drop to uid 101 like dbus-daemon, then execve a set-uid file (dbus-daemon-launch-helper) */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#define _GNU_SOURCE
extern int getresuid(uid_t *, uid_t *, uid_t *);
int main(int argc, char **argv) {
    if (argc > 1) {                         /* helper side: print the ids this image started with */
        uid_t r, e, s; getresuid(&r, &e, &s);
        printf("uid r=%u e=%u s=%u\n", r, e, s); return 0;
    }
    if (setgid(101) || setuid(101)) { perror("drop"); return 1; }
    execl("/usr/lib/h", "h", "x", (char *) 0); perror("exec"); return 1;
}

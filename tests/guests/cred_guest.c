/* guest for tests/integration/cred.sh: SCM_CREDENTIALS and SO_PEERCRED under a fake root (what GDBus does when it connects) */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
int main(int argc, char **argv) {
    int sv[2], on = 1; char c = 0, r = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) { perror("socketpair"); return 1; }
    setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    char cb[CMSG_SPACE(sizeof(struct ucred))]; memset(cb, 0, sizeof cb);
    struct iovec iov = { &c, 1 };
    struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cb, .msg_controllen = sizeof cb };
    struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
    cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_CREDENTIALS; cm->cmsg_len = CMSG_LEN(sizeof(struct ucred));
    struct ucred u = { getpid(), getuid(), getgid() }; memcpy(CMSG_DATA(cm), &u, sizeof u);
    long s = sendmsg(sv[0], &m, 0);
    printf("send creds uid=%d gid=%d: %s\n", (int) u.uid, (int) u.gid, s == 1 ? "ok" : strerror(errno));
    if (s != 1) return 2;
    if (argc > 1) {                      /* "drop" like dbus-daemon (root -> messagebus) AFTER the peer sent: the peer must still be shown as the session's id */
        unsigned d = (unsigned) atoi(argv[1]);
        if (setresgid(d, d, d) || setresuid(d, d, d)) { perror("drop"); return 5; }
        printf("dropped to uid=%d\n", (int) getuid());
    }
    char rb[CMSG_SPACE(sizeof(struct ucred))]; memset(rb, 0, sizeof rb);
    struct iovec iv2 = { &r, 1 };
    struct msghdr rm = { .msg_iov = &iv2, .msg_iovlen = 1, .msg_control = rb, .msg_controllen = sizeof rb };
    if (recvmsg(sv[1], &rm, 0) != 1) { perror("recvmsg"); return 3; }
    struct cmsghdr *rc = CMSG_FIRSTHDR(&rm);
    if (rc && rc->cmsg_type == SCM_CREDENTIALS) { struct ucred x; memcpy(&x, CMSG_DATA(rc), sizeof x); printf("recv creds uid=%d gid=%d\n", (int) x.uid, (int) x.gid); }
    else printf("recv creds: none\n");
    struct ucred pc; socklen_t l = sizeof pc;
    if (getsockopt(sv[1], SOL_SOCKET, SO_PEERCRED, &pc, &l)) { perror("peercred"); return 4; }
    printf("peercred uid=%d gid=%d\n", (int) pc.uid, (int) pc.gid);
    return 0;
}

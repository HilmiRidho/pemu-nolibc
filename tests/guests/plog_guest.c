/* Freestanding guest used by tests/integration/plog.sh (x86_64 under `pemu`; raw syscalls, no libc).
 * It does things pemu must write down in .pemu/pemu.log, and prints nothing: the log must never reach the guest's stdout / stderr.
 *   argv[1] absent : unsupported syscall (6 times), rseq (ENOSYS), ptrace (refused), unshare(CLONE_NEWNS) (refused), then exit 0
 *   argv[1] "ill"  : an instruction pemu does not implement (SIGILL)
 *   argv[1] "exec" : execve itself with HOME changed to a directory that must not be used, then behave as with no argument */
typedef unsigned long u64; typedef long i64;
enum { S_exit = 231, S_ptrace = 101, S_unshare = 272, S_rseq = 334, S_execve = 59 };
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 r10 __asm__("r10") = d, r8 __asm__("r8") = e, r9 __asm__("r9") = f;
    i64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#define ILL() __asm__ volatile("ud2")
void pemu_main(u64 *sp) {
    int argc = (int) sp[0];
    char **av = (char **) (sp + 1);
    if (argc > 1 && av[1][0] == 'i') { ILL(); }
    if (argc > 1 && av[1][0] == 'e') {
        char *nav[2] = { av[0], 0 }; char *env[2] = { "HOME=/nonexistent-plog-home", 0 };
        sc(S_execve, (i64) av[0], (i64) nav, (i64) env, 0, 0, 0);
        sc(S_exit, 99, 0, 0, 0, 0, 0);
    }
    for (int i = 0; i < 6; i++) sc(1000, 1, 2, 3, 0, 0, 0);        /* no such syscall in any ABI: unsupported-syscall, x6 */
    sc(S_rseq, 0, 0, 0, 0, 0, 0);                                  /* answered ENOSYS on purpose */
    sc(S_ptrace, 16, 0x7ffffff0, 0, 0, 0, 0);                      /* refused (a pid that cannot exist, in case the refusal were missing) */
    sc(S_unshare, 0x20000 | 1, 0, 0, 0, 0, 0);                     /* CLONE_NEWNS | an invalid bit: refused before the host sees it */
    sc(S_exit, 0, 0, 0, 0, 0, 0);
}
__asm__(".global _start\n_start:\n mov %rsp, %rdi\n and $-16, %rsp\n call pemu_main\n");

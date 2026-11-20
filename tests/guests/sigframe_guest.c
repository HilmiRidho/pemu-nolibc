/* Guest of tests/integration/sigframe.sh: a SIGSEGV handler (no sigaltstack) and a stack overflow.  The kernel cannot build the signal frame on the
 * exhausted stack and kills the process with SIGSEGV; the handler must NOT run.  Freestanding x86_64, raw syscalls. */
typedef unsigned long u64;
static long sc(long n, long a, long b, long c, long d) {
    long r; register long r10 __asm__("r10") = d;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}
static void handler(int s) { (void) s; sc(60, 7, 0, 0, 0); }          /* exit(7): reaching it means the frame was built where it cannot be */
static int rec(volatile int n) { volatile char b[4096]; b[0] = (char) n; return rec(n + 1) + b[0]; }
void _start(void) {
    struct { u64 h, flags, restorer, mask; } sa = { (u64) handler, 0x04000000 /*SA_RESTORER*/, 0, 0 };
    sc(13, 11 /*SIGSEGV*/, (long) &sa, 0, 8);
    rec(0);
    sc(60, 3, 0, 0, 0);
}

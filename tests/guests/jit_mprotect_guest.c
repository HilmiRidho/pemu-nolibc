/* Guest for tests/integration/jit_mprotect.sh: generated code that is re-protected, replaced and unmapped, as Wine, V8 and Mono do.
 * Prints the value each call returns; the script checks them and counts the JIT cache flushes. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
typedef int (*fn_t)(void);
static void put(void *p, int v) { unsigned char c[6] = { 0xb8, 0, 0, 0, 0, 0xc3 }; memcpy(c + 1, &v, 4); memcpy(p, c, 6); }   /* mov eax, v ; ret */
int main(void) {
    unsigned char *pg = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pg == MAP_FAILED) return 2;
    put(pg, 101);
    if (mprotect(pg, 4096, PROT_READ | PROT_EXEC)) return 3;
    printf("v1 %d\n", ((fn_t) pg)());
    int sum = 0;
    for (int i = 0; i < 20; i++) {                              /* r-x over a translated r-x page: nothing to invalidate */
        if (mprotect(pg, 4096, PROT_READ | PROT_EXEC)) return 4;
        sum += ((fn_t) pg)();
    }
    printf("rx-rx %d\n", sum);
    if (mprotect(pg, 4096, PROT_READ | PROT_WRITE)) return 5;   /* the page stops being executable: this one must flush */
    put(pg, 202);
    if (mprotect(pg, 4096, PROT_READ | PROT_EXEC)) return 6;
    printf("v2 %d\n", ((fn_t) pg)());
    /* a large reservation far away from the code (Wine maps these constantly): neither the mmap nor the munmap may flush */
    for (int i = 0; i < 20; i++) {
        void *big = mmap(NULL, 1ULL << 30, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (big == MAP_FAILED) return 7;
        if (munmap(big, 1ULL << 30)) return 8;
        printf("%s", i == 19 ? "far ok\n" : "");
    }
    if (((fn_t) pg)() != 202) return 9;
    void *q = mmap(pg, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);   /* replaced under translated code: must flush */
    if (q != (void *) pg) return 10;
    put(pg, 303);
    if (mprotect(pg, 4096, PROT_READ | PROT_EXEC)) return 11;
    printf("v3 %d\n", ((fn_t) pg)());
    if (munmap(pg, 4096)) return 12;                            /* unmapped code: must flush */
    unsigned char *p2 = mmap(pg, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);   /* rwx: interpreted, still correct after a rewrite */
    if (p2 != pg) return 13;
    put(pg, 404); printf("rwx1 %d\n", ((fn_t) pg)());
    put(pg, 505); printf("rwx2 %d\n", ((fn_t) pg)());
    return 0;
}

/* brk shrink then regrow must give zero pages (glibc calloc relies on it) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    enum { N = 64, SZ = 100 * 1024 };
    void *p[N]; int bad = 0;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < N; i++) { p[i] = malloc(SZ); if (!p[i]) return 2; memset(p[i], 0xAA, SZ); }
        void *b1 = sbrk(0);
        for (int i = 0; i < N; i++) free(p[i]);          /* top > trim threshold: glibc shrinks brk */
        void *b2 = sbrk(0);
        for (int i = 0; i < N; i++) {
            unsigned char *q = calloc(1, SZ); if (!q) return 2;
            for (int k = 0; k < SZ; k++) if (q[k]) { bad++; break; }
            p[i] = q; memset(q, 0xAA, SZ);
        }
        for (int i = 0; i < N; i++) free(p[i]);
        printf("round %d: brk %s (%ld KiB released), calloc non-zero so far: %d\n", round, b2 < b1 ? "shrunk" : "NOT shrunk", (long)((char*)b1 - (char*)b2) / 1024, bad);
    }
    puts(bad ? "FAIL: calloc returned non-zero memory" : "ok");
    return bad != 0;
}

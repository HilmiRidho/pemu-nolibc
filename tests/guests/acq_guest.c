/* Aligned and misaligned integer loads / stores of every size (including the sign-extending forms) after the guest has a second thread.
 * Output: one checksum per size and offset class.  Interpreter and JIT must agree (tests/integration/acq.sh). */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void *idle(void *a) { return a; }
#define LD(T, o)   (*(volatile T *)(b + (o)))
#define ST(T, o, v) (*(volatile T *)(b + (o)) = (T) (v))
typedef struct { uint16_t v; } __attribute__((packed)) P16;
static uint64_t mix(uint64_t h, uint64_t x) { return (h ^ x) * 1099511628211ull; }
__attribute__((noinline)) static uint64_t run(unsigned char *b, int seed) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 256; i++) b[i] = (unsigned char) (i * 37 + seed);
    for (int r = 0; r < 64; r++) {
        int o = (r * 5 + seed) & 63;                       /* offset class: aligned and not */
        h = mix(h, (uint64_t) LD(int8_t, o));              /* movsx r64, byte */
        h = mix(h, (uint64_t) (int32_t) LD(int8_t, o + 1));/* movsx r32, byte */
        h = mix(h, LD(uint8_t, o + 2));
        h = mix(h, (uint64_t) LD(int16_t, o + 3));
        h = mix(h, (uint64_t) (int32_t) LD(int16_t, o + 5));
        h = mix(h, LD(uint16_t, o + 7));
        h = mix(h, (uint64_t) LD(int32_t, o + 9));         /* movsxd */
        h = mix(h, LD(uint32_t, o + 13));
        h = mix(h, LD(uint64_t, o + 17));
        h = mix(h, LD(uint64_t, (o & ~7) + 24));           /* aligned */
        h = mix(h, LD(uint32_t, (o & ~3) + 32));
        h = mix(h, (uint64_t) LD(int16_t, (o & ~1) + 36));
        ST(uint8_t, o + 100, h);
        ST(uint16_t, o + 102, h >> 3);
        ST(uint32_t, o + 105, h >> 7);
        ST(uint64_t, o + 109, h);
        ST(uint64_t, (o & ~7) + 136, h);
        ST(uint32_t, (o & ~3) + 144, h);
        ST(uint16_t, (o & ~1) + 148, h);
        ST(uint64_t, o + 150, 0);                          /* store of zero */
        h = mix(h, LD(uint64_t, o + 100));
    }
    for (int i = 0; i < 256; i++) h = mix(h, b[i]);
    return h;
}
int main(void) {
    pthread_t t; pthread_create(&t, NULL, idle, NULL); pthread_join(t, NULL);       /* from here on the JIT emits the x86 memory-ordering barriers */
    static unsigned char buf[512] __attribute__((aligned(16)));
    uint64_t h = 0;
    for (int s = 0; s < 8; s++) { uint64_t r = run(buf, s); h = mix(h, r); printf("seed %d: %016llx\n", s, (unsigned long long) r); }
    printf("acq_guest: %016llx\n", (unsigned long long) h);
    return 0;
}

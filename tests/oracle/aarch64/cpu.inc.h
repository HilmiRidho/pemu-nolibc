/* tests/oracle/aarch64/cpu.inc.h - guest CPU state, types, host includes. SPDX-License-Identifier: Apache-2.0 */
/* AArch64 interpreter kept as a test oracle: it executes the AArch64 code that the JIT of pemu emits (see oracle.c). Not a guest of pemu. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <math.h>
#include <elf.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/auxv.h>
#include <sys/random.h>
#include <stddef.h>
#include <ucontext.h>
#include "../../../host/pemu_host.h"   /* every call that leaves the emulator for the host kernel (see there) */

#ifndef __x86_64__
#error "the AArch64 oracle runs on x86_64 hosts only"
#endif

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int64_t s64; typedef int32_t s32;
typedef u16 __attribute__((may_alias, aligned(1))) u16u;
typedef u32 __attribute__((may_alias, aligned(1))) u32u;
typedef u64 __attribute__((may_alias, aligned(1))) u64u;

typedef struct {
    u64 x[32];          /* x[31] unused: sp kept separately */
    u64 sp, pc, tpidr, fpcr, fpsr;
    u32 nzcv;           /* N=8 Z=4 C=2 V=1 */
    u64 v[32][2];
    u64 xaddr, xval; int xsz;   /* exclusive monitor */
    u64 xfl, jflag, patch;      /* JIT: saved x86 flags, async-signal flag, chain patch site */
} CPU;



/* Exit-request word, polled by the JIT; set by the OS layer's host signal handlers. */
static volatile u64 pend;

/* ------------------------------------------------------------ interface to the OS layer
 * The CPU core never calls into an OS layer by name: everything that needs an operating system goes through
 * this table, which an OS layer installs before the first guest instruction runs.
 *   syscall(c)       the guest executed `svc`; c->pc is already the next instruction, the arguments are in c->x[]
 *   exception(c, e)  the CPU raised exception e (EXCP_*). It may return, and the core then carries on, or it may terminate
 *   pending(c)       the exit-request word is non-zero: deliver the queued guest signals
 *   host_fault(c)    a host fault in guest code came back through g_jb: turn it into a guest signal
 *   negative(k,key,m) record an event that is a limit or a failure of the emulator (see PEMU_NEG below); may be NULL */
enum { EXCP_UDEF = 1, EXCP_BRK = 7 };
typedef struct PemuOS {
    void (*syscall)(CPU *c);
    void (*exception)(CPU *c, int excp);
    void (*pending)(CPU *c);
    void (*host_fault)(CPU *c);
    void (*negative)(const char *kind, u64 key, const char *msg);   /* write down something pemu cannot do (OS layer: .pemu/pemu.log) */
} PemuOS;
static const PemuOS *g_os;

/* negative events (unsupported instruction, JIT fallback, fatal fault) go to the OS layer's log through PemuOS.negative; with no OS layer
 * (the isolated core check) they are dropped.  kind: short constant, key: what makes two events the same (0 = the message), then printf. */
#define PEMU_NEG(kind_, key_, ...) do { if (g_os && g_os->negative) { char nb_[200]; snprintf(nb_, sizeof nb_, __VA_ARGS__); g_os->negative((kind_), (u64) (key_), nb_); } } while (0)

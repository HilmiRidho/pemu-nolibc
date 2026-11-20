/* tests/oracle/aarch64/oracle.c - AArch64 interpreter used ONLY as a CPU stand-in by the JIT tests (jit_embed, jit_cache, jit_mprotect, acq).
 * pemu itself no longer runs AArch64 guests. Build: cc -DPEMU_UNIT_TEST_LIBC -DPEMU_EMBED -c tests/oracle/aarch64/oracle.c
 * and link it next to src/pemu.c compiled with -DPEMU_JIT_EMBED: the AArch64 code that pemu's JIT emits is then executed here (see embed_hook.inc.h). */
#include "cpu.inc.h"
#include "helpers.inc.h"
#include "mem.inc.h"
#include "load_store.inc.h"
#include "fp_simd.inc.h"
#include "step.inc.h"
#include "embed_hook.inc.h"

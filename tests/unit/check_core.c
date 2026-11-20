/* tests/unit/check_core.c - the CPU core and the JIT must compile without the OS layer (guest/x86_64/linux/) and without src/proot/.
 * build.sh runs: cc -fsyntax-only -Werror=implicit-function-declaration -nostdinc -Ihost/abi/include -I. tests/unit/check_core.c
 * A direct call from the core into the OS layer is an implicit declaration here; it has to go through the PemuOS table (cpu.inc.h). */
#include "src/guest/x86_64/cpu/all.inc.h"
#ifdef HAVE_JIT
#include "src/jit/x86_64-aarch64/all.inc.h"
#endif

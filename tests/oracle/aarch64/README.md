# tests/oracle/aarch64/ - an AArch64 interpreter for the JIT tests

This is **not** an AArch64 guest of `pemu`. `pemu` runs x86_64 programs only; AArch64 guest support was removed.

What this directory is: a small AArch64 interpreter (`oracle.c` and the `*.inc.h` next to it). The JIT back end in `src/jit/x86_64-aarch64/` translates x86_64 to AArch64
machine code. On an x86_64 machine that code cannot run natively, so the JIT tests build `src/pemu.c` with `-DPEMU_JIT_EMBED` and hand every block the JIT emits to
this interpreter (`embed_hook.inc.h`). That is how the aarch64 back end is tested without an aarch64 CPU.

- Used by: `tests/integration/jit_embed.sh`, `tests/integration/jit_cache.sh`, `tests/integration/jit_mprotect.sh`, `tests/integration/acq.sh` (four JIT tests). Removing this directory removes those tests.
- Never linked into the `pemu` binary: `build.sh` does not compile it, and `nm -u` / the `nolibc` checks cover `pemu` only.
- Compiled with the system's libc (`-DPEMU_UNIT_TEST_LIBC -DPEMU_EMBED`), like the other host-side test programs.
- What it cannot show: how a real CPU behaves (memory ordering, cache maintenance, FPCR/FPSR corners). That is what `tests/manual/jit_hw.sh` is for, on a device.

#!/bin/sh
# Build pemu, check the CPU core in isolation, then run the tests. Run from anywhere; the script cd's to its own directory.
#
# Layout : src/pemu.c is the entry file (one translation unit); the parts are listed by manifests, see docs/ARCHITECTURE.md:
#          src/guest/x86_64/{cpu,linux}/ (x86_64 guest: interpreter + Linux syscall/signal/ELF layer), src/jit/x86_64-aarch64/ (JIT for aarch64 hosts),
#          src/proot/ (built-in proot: rootfs, bind mounts, fake root; arch neutral). host/ = libc-free runtime and ABI headers.
#          tests/ = unit/ (host-native C), integration/ (end-to-end and static checks), oracle/aarch64/ (AArch64 interpreter that runs the JIT's output), see tests/README.md.
#
# Output (in $OUT):
#   pemu      x86_64 Linux guest emulator (interpreter on x86_64 hosts, interpreter + JIT on aarch64 hosts: Termux / Android / arm64 Linux)
#
# Version: read from ./VERSION and compiled in as PEMU_RELEASE.
# Environment: CC (compiler), GUEST (guest architecture = a directory of src/guest/, default x86_64), ARCH (target arch, default uname -m), OUT (output dir, default ./build),
#              SKIP_TESTS=1 (cross builds: the test binaries cannot run on the build machine)
#              PEMU_HOP_SCRAMBLE=1 (test build): the HOP_ host-operation numbers are not Linux numbers (host/pemu_hop.h); proves src/ does not rely on them. Same tests must pass.
#              PEMU_REQUIRE_TESTS=1 (CI): a test that cannot run because a tool is missing (clang / lld, python3, libquadmath) FAILS instead of printing "skipping" and passing.
#              There is no libc in this build: not linked and not one of its headers read (-nostdlib -nostdinc -static-pie; host/rt/ is the runtime, host/abi/include/ the ABI).
#                nm -u is empty, ldd says "statically linked". x86_64 tested; aarch64 compiles (cross-checked), its asm has not run on hardware.
#              OPT (optimisation flags of pemu, default -O2). Try OPT=-O3, or a PGO build:
#                OPT="-O2 -fprofile-generate" ./build.sh, run a typical guest workload, then
#                OPT="-O2 -fprofile-use -fprofile-correction" ./build.sh   (same compiler, same sources). Compare on your own workload.
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
ARCH=${ARCH:-$(uname -m)}
GUEST=${GUEST:-x86_64}
[ -d "src/guest/$GUEST" ] || { echo "build.sh: unknown GUEST '$GUEST' (no src/guest/$GUEST/); one of: $(ls src/guest | tr '\n' ' ')" >&2; exit 1; }
OUT=${OUT:-build}
# Distro compilers switch on hardening by default (stack protector, FORTIFY, CET/BTI/PAC, stack clash): each costs cycles in the interpreter loop and in the
# JIT's C helpers, and pemu runs one trusted emulator image, so they are off here. Every flag is probed, an unknown one is dropped. OPT=... replaces all of it.
FAST=""
for f in -fno-stack-protector -U_FORTIFY_SOURCE -fno-stack-clash-protection -fcf-protection=none -mbranch-protection=none -fno-plt -fomit-frame-pointer; do
  echo 'int x;' | $CC -Werror $f -x c -c - -o /dev/null 2>/dev/null && FAST="$FAST $f"
done
# NATIVE=1: tune for the CPU this runs on (-mcpu=native on aarch64, -march=native on x86_64). Only for a build that stays on the machine that made it, not for a .deb.
[ -n "$NATIVE" ] && for f in -mcpu=native -march=native; do
  echo 'int x;' | $CC -Werror $f -x c -c - -o /dev/null 2>/dev/null && { FAST="$FAST $f"; break; }
done
OPT=${OPT:--O2$FAST}
# No libc at all: the runtime in host/rt/ is compiled in with the emulator (pemu_host.h includes it), the ABI comes from host/abi/include (-nostdinc: no system header),
# libgcc is the compiler's own (soft-float, 128-bit division), not libc.  Static-pie, NOT plain -static: a non-PIE image sits at 0x400000, where pemu maps the guest 1:1.
case "$ARCH" in
  x86_64|amd64) ;;
  aarch64|arm64) echo "build.sh: aarch64 host: compiles with no libc header (checked by cross-compiling), long double math is host/rt/rt_quad.h (checked on x86_64), but the aarch64 code of host/rt/ has NOT run on real hardware yet" >&2 ;;
  *) echo "build.sh: unsupported ARCH '$ARCH' (x86_64 and aarch64 hosts)" >&2; exit 1 ;;
esac
# aarch64 distro gcc (Debian 12) and recent clang call the LSE "outline atomics" helpers in libgcc.a / compiler-rt, whose lse-init.o constructor needs
# __getauxval from a libc: not linked here.  Inline LL/SC atomics instead (the JIT picks its own LSE paths at run time).  Probed: x86_64 gcc has no such flag.
NOOA=""
echo 'int x;' | $CC -Werror -mno-outline-atomics -x c -c - -o /dev/null 2>/dev/null && NOOA="-mno-outline-atomics"
LDS="-static-pie -fPIE -nostdlib -nostdinc -I$(pwd)/host/abi/include -U_FORTIFY_SOURCE -fno-math-errno -fno-stack-protector -falign-functions=64 -falign-jumps=16 $NOOA"
# The compiler's runtime library: libgcc.a for gcc, libclang_rt.builtins-*.a for clang with compiler-rt (Android NDK has no libgcc.a, so -lgcc fails there).
# Ask the compiler for its file; fall back to -lgcc when it names none that exists.
MATH="-lgcc"
LIBRT=$($CC -print-libgcc-file-name 2>/dev/null) || LIBRT=""
[ -n "$LIBRT" ] && [ -f "$LIBRT" ] && MATH="$LIBRT"
VERDEF="-DPEMU_RELEASE=\"$(tr -d ' \n' < VERSION)\""   # single source of truth: ./VERSION
mkdir -p "$OUT"
# host/pemu_hop.h (the table of HOP_ host operations) is generated from the names src/ uses: it must be current
if command -v python3 >/dev/null 2>&1; then python3 tools/gen_hop.py --check || exit 1; fi
HOPDEF=""; [ -n "$PEMU_HOP_SCRAMBLE" ] && HOPDEF="-DPEMU_HOP_SCRAMBLE=1"
$CC $OPT $VERDEF $HOPDEF -DPEMU_GUEST_$GUEST=1 $LDS -o "$OUT/pemu" src/pemu.c $MATH
# the CPU core (src/guest/x86_64/cpu/ + src/jit/x86_64-aarch64/) must not depend on src/guest/x86_64/linux/: compile it alone. Anything it needs from the OS goes through
# the PemuOS table (src/guest/x86_64/cpu/cpu.inc.h), so a direct call to a src/guest/x86_64/linux/ function fails here (see tests/unit/check_core.c)
CHK="-fsyntax-only -Werror=implicit-function-declaration -nostdinc -I$(pwd)/host/abi/include -I."
$CC $CHK tests/unit/check_core.c
case "$ARCH" in x86_64|amd64) $CC $CHK -DPEMU_JIT_EMBED tests/unit/check_core.c ;; esac
echo "core check: src/guest/x86_64/cpu/ + src/jit/x86_64-aarch64/ compile without src/guest/x86_64/linux/"
# every call that leaves the emulator for the host kernel goes through host/pemu_host.h (road map step 1); a direct mmap / fork / syscall fails here
if command -v python3 >/dev/null 2>&1; then python3 tools/check_host_seam.py
elif [ -n "$PEMU_REQUIRE_TESTS" ]; then echo "build.sh: python3 missing and PEMU_REQUIRE_TESTS is set: the host seam check cannot run" >&2; exit 1
else echo "build.sh: python3 missing, host seam check skipped" >&2; fi
# tests: tests/run.sh finds them (tests/unit/test_*.c, tests/integration/*.sh); it needs $OUT/pemu. SKIP_TESTS=1 skips them (cross builds).
[ -n "$SKIP_TESTS" ] || OUT="$OUT" CC="$CC" sh tests/run.sh

/* pemu - x86_64 Linux guest user-mode emulator (entry file; one translation unit, see the manifests below). SPDX-License-Identifier: Apache-2.0
 *
 * src/guest/<arch>/cpu/    guest CPU core: the interpreter (x86_64)                   -- arch specific
 * src/guest/<arch>/linux/  Linux ABI layer of that guest: syscalls, signals, ELF, main -- arch specific
 * src/os/linux/            parts of the Linux layer any 64-bit guest shares (host syscall hook-up, signals, brk, ELF loader, execve, threads, main) -- arch neutral
 * src/jit/x86_64-aarch64/<guest>-<host>/  JIT back end for one guest/host pair (x86_64 -> aarch64)    -- only when HAVE_JIT
 * src/proot/               built-in proot: rootfs, bind mounts, fake root, seccomp...  -- arch neutral
 * host/                    libc-free runtime (rt/), kernel ABI headers (abi/), host seam (pemu_host.h)
 * See docs/ARCHITECTURE.md for how to add a guest, a JIT pair or a host. */
#ifndef PEMU_RELEASE
#define PEMU_RELEASE "dev"   /* build.sh sets it from the VERSION file */
#endif
/* Guest selection: build.sh GUEST=<arch> passes -DPEMU_GUEST_<arch>=1; with none given the guest is x86_64.
 * A new guest is one more branch below (its directory is src/guest/<arch>/, see docs/ARCHITECTURE.md). */
#if !defined(PEMU_GUEST_x86_64)
#define PEMU_GUEST_x86_64 1
#endif
#if defined(PEMU_GUEST_x86_64)
#include "guest/x86_64/cpu/all.inc.h"
#ifdef HAVE_JIT
#include "jit/x86_64-aarch64/all.inc.h"
#endif
#include "guest/x86_64/linux/all.inc.h"
#else
#error "no guest selected: see the guest switch at the top of src/pemu.c"
#endif

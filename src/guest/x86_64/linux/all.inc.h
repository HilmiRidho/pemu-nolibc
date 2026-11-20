/* src/guest/x86_64/linux/all.inc.h - manifest: Linux layer for an x86_64 guest. SPDX-License-Identifier: Apache-2.0
 * Comes after the CPU core (guest/x86_64/cpu/) and the JIT, which it drives. src/os/linux/ holds what every 64-bit guest shares (host syscall hook-up, signal state
 * and host handlers, brk, ELF loader, execve, threads / clone, /proc/self, main); the files here are the x86_64 part. Order matters: each file uses what the ones above it define. */
#include "../../../os/linux/host_syscall.inc.h"   /* also pulls in the arch-neutral built-in proot (src/proot/) */
#include "guest.inc.h"                            /* x86_64 hooks for the shared files: ELF machine, auxv, registers, thread / clone, fault pc */
#include "../../../os/linux/signal.inc.h"
#include "sigframe.inc.h"                         /* x86_64 signal frame (deliver_sig, rt_sigreturn) */
#include "abi.inc.h"                              /* open flags, struct stat, MAP_32BIT; includes os/linux/brk.inc.h */
#include "../../../os/linux/execve.inc.h"
#include "sysnum.inc.h"                           /* x86_64 syscall numbers that pass through */
#include "../../../os/linux/thread.inc.h"
#include "../../../os/linux/procself.inc.h"
#include "procfake.inc.h"                         /* /proc/cpuinfo, /proc/self/exe patching, getdents */
#include "syscall.inc.h"                          /* dispatch: pemu_sys, gsys, do_syscall */
#include "cpu_loop.inc.h"
#include "../../../os/linux/elfload.inc.h"
#include "../../../os/linux/main.inc.h"

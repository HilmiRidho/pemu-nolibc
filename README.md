# pemu

Run x86_64 Linux programs on aarch64 hosts, with a built-in chroot, bind mounts and fake root.
No `ptrace` and no helper process needed.

| binary | runs programs for | on host |
|---|---|---|
| `pemu` | x86_64 | aarch64 (Termux, Debian arm64): interpreter + JIT |
| `pemu` | x86_64 | x86_64 (Debian, Ubuntu ...): interpreter only |

On an x86_64 host `pemu` builds and runs as an interpreter only; that is how it is tested. AArch64 guests are not supported.

**Status of the aarch64 JIT.** The AArch64 code the JIT emits is tested on x86_64 by running it on an interpreter (`tests/oracle/aarch64/`, `tests/integration/jit_embed.sh`). It has **not yet been run on real aarch64 hardware** by the maintainers: `tests/manual/jit_hw.sh` is the test for that (build the guests and the reference output on x86_64, run them on the device). Reports from a device are welcome.

License: Apache-2.0.

## Install

From a `.deb` (see *Actions → Artifacts*, or *Releases* for tagged versions):

    sudo apt install ./pemu_<version>_debian_arm64.deb         # Debian / Ubuntu on arm64 (interpreter + JIT)
    sudo apt install ./pemu_<version>_debian_amd64.deb         # Debian / Ubuntu on x86_64 (interpreter only)
    apt install ./pemu_<version>_termux_aarch64.deb            # Termux

From source (needs a C compiler):

    ./build.sh            # output in ./build, also runs the tests

There is no libc in the build (not linked, no header read): the result is a `-static-pie` image, `nm -u` is empty. A plain `-static` link would not work: the image would sit at 0x400000, where pemu maps the guest 1:1.

To remove: `sudo apt remove pemu`. Installed rootfs are not deleted; use `pemu uninstall NAME` first.

## Usage

    pemu login debian                        # enter a rootfs as fake root
    pemu login debian apt update             # run one command
    pemu login debian -b $HOME:/mnt/home     # share a host folder
    pemu install debian rootfs.tar.xz        # unpack a rootfs tarball
    pemu list                                # show installed rootfs
    pemu uninstall debian                    # delete a rootfs (asks first)

Without `login`, `pemu program [args...]` runs one program with no rootfs.

Options go **after** the rootfs name:

| option | meaning |
|---|---|
| `-b, --bind SRC[:DST]` | share a host path (repeatable) |
| `-w, --cwd DIR` | starting directory (default `/root`) |
| `-e, --env VAR=val` | set an environment variable |
| `-i, --change-id UID[:GID]` | use this fake id instead of root |
| `--no-root-id` | do not fake root |
| `-l, --link2symlink` | emulate hard links with symlinks |
| `--no-default-binds` | do not share `/dev /proc /sys` |

Rootfs are looked up in `~/.pemu/rootfs` (or `$PEMU_ROOTFS_DIR`).

## What works

- File paths inside the rootfs: open, stat, rename, chmod, chown, symlinks, `execve`, `#!` scripts and more.
  Absolute symlinks and `..` stay inside the rootfs.
- Unix sockets, `mount -o bind` / `umount` (other mount types are refused), and `chroot`.
- `/proc/mounts` and `mountinfo` show only what the guest can see, so `df` and `findmnt` work without errors.
- On Android, `/proc` files that the system hides (`stat`, `loadavg`, `uptime`, ...) are faked.
- `chown` by fake root is remembered, so `ls -l`, `tar` and `dpkg` agree.

## Speed (x86_64 guest on aarch64, e.g. Wine)

Find out what is slow before tuning: `PEMU_JIT_STATS=/sdcard/pemu-stats` (a path: one file per process, `<path>.<pid>`; without a path it goes to stderr, which Wine
mostly loses) prints, when each process exits, the number of translated blocks, **cache flushes**, instructions that still run in the interpreter and the most executed
ones. Many flushes means the guest unmaps or re-protects code (or the cache is full); many executed fallbacks means an instruction without a translation (also listed in
`~/.pemu/pemu.log` as `jit-fallback`). `PEMU_JIT=0` turns the JIT off, which shows how much it brings.

Build for the machine it runs on (not for a `.deb`): `NATIVE=1 ./build.sh`, and try `OPT=-O3` or the PGO build described at the top of `build.sh`; compare on your own workload.
Wine settings that cost nothing in the emulator: `WINEDEBUG=-all`, `WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"` (no Mono/Gecko prompts, no menu entries),
a persistent server (`wineserver -p`) and one prefix that is reused (creating a prefix is the slowest thing Wine does).

**Persistent translation cache (opt-in).** `PEMU_JIT_CACHE=/absolute/dir` keeps the blocks a process translated and reuses them in later runs, so a program (and every
Wine process that loads the same DLLs) is translated once instead of at every start. Off by default.

- What is reused: a block, only when the guest bytes it was made from are still at the same address (a hash of the guest code pages is checked), with the same
  JIT options. A library mapped elsewhere, or another build of it, is translated again; stale blocks are never run. The cache is one file per pemu binary
  (`jit-<hash of the binary>.pjc`), so a new build starts with an empty one; old files can simply be deleted (not truncated) when no pemu runs.
- Blocks are written when a process exits or execs, and whenever 1 MiB is pending; a process killed by a signal loses what it had not written. The file stops
  growing at 128 MiB. Several processes may share it.
- Statistics: with `PEMU_JIT_STATS` the report has a `pemu jit cache:` line (blocks loaded / translated, time spent in each, blocks recorded).
- Only the first pemu reads the environment variable; processes the guest starts get the choice as a hidden option, so a guest cannot change the directory.
- **Trust**: the code in the file runs in the emulator, outside the guest. The directory and the file must belong to you and be closed to others (the directory is
  created with mode 700; anything else is ignored); keep it out of every path the guest can write to (a bind of `$HOME`, say). The checksum protects against damage,
  it is not a signature.
- How much it brings depends on how much of a run is translation. In the x86_64 test build, copying a block from the cache took about half the time of translating it
  (the part of a run that translation is depends on the program); measure on your own workload with `PEMU_JIT_STATS`.
- Tests: `tests/integration/jit_cache.sh` (also part of `build.sh`). Tests of the JIT run the AArch64 code it emits on a small interpreter kept for that purpose (`tests/oracle/aarch64/`, never part of `pemu`). Not covered by any test on an x86_64 host: the AArch64 `adrp` form of guest addresses, which the cache turns off
  on purpose (`a_movia`).

## Electron and other Chromium apps (x86_64 guest)

Electron / Chromium find their files through `/proc/self/exe` and start every child process (renderer, GPU, utility) by
executing `/proc/self/exe` again. Under pemu that path used to be the emulator itself, so the app could not find its
resources and its children started the emulator with no program. Now, with or without a rootfs:

- `readlink`, `open`, `stat` and `execve` of `/proc/self/exe` (and `/proc/<pid>/exe`) refer to the guest program;
- `/proc/self/cmdline` is the guest's `argv`;
- a guest's seccomp filter is run by the emulator, never installed on the host process (see "Guest seccomp" below).

pemu has no user namespaces, so Chromium's sandbox cannot start (its seccomp layer would work, see below). Tell Chromium so:

    pemu login debian -e DISPLAY=:0 -- /opt/MyApp/myapp --no-sandbox --no-zygote --disable-gpu

`--no-sandbox` is required; `--no-zygote` and `--disable-gpu` keep the process tree and the graphics stack simple under
emulation (software rendering only). Expect it to be slow, especially on a host without the JIT (x86_64).

Checked so far with Node.js (V8, libuv, worker threads, WebAssembly traps, `fork` with fd passing) and with small static
guests (`tests/integration/selfexe.sh`). A real Electron app has **not** been run yet; see `docs/REVIEW.md`, round 5.

## Guest seccomp

A guest can restrict itself the way it would on Linux: `prctl(PR_SET_NO_NEW_PRIVS)`, `prctl(PR_SET_SECCOMP, 1|2)` and
`seccomp(SET_MODE_STRICT | SET_MODE_FILTER | GET_ACTION_AVAIL)`. A filter holds the guest's syscall numbers, so it is never given to
the host kernel; pemu validates it like the kernel does and runs it (a small classic-BPF interpreter) on every guest syscall, before
anything else. Actions: `ALLOW`, `LOG`, `ERRNO`, `TRAP` (SIGSYS to the guest's handler, with `si_syscall`, `si_arch`, `si_call_addr`
and `si_errno`; no handler: the process dies), `KILL_THREAD`, `KILL_PROCESS`; `TRACE` and `USER_NOTIF` answer `ENOSYS` (no tracer or
supervisor). With several filters the lowest action wins and the newest filter wins a tie, as on Linux.

Filters are inherited by `fork` and `clone` (also by new threads), survive `execve` (the new pemu image receives them as a sealed
memfd through the hidden `--seccomp-fd`; if that fails the exec fails, an image never runs unfiltered; and an `execve` of a program
that is not a guest binary is refused with `ENOEXEC` while a filter is active, as it would run unfiltered), and `TSYNC` filters apply
to every thread. Not supported (`EINVAL`): `NEW_LISTENER` (user notification) and `WAIT_KILLABLE_RECV`.

What it is not: a security boundary (pemu is not one), and two small differences from a native process: the guest has no vDSO, so
calls such as `clock_gettime` also pass through the filter, and `/proc/self/status` still shows the host's `Seccomp:` line (use
`prctl(PR_GET_SECCOMP)`). A sandbox that also needs user namespaces (Chromium's) still has to run with `--no-sandbox`.
Tests: `tests/unit/test_seccomp.c` (validator, interpreter, precedence, hand-over) and `tests/integration/seccomp.sh` (x86_64 guest).

## Other processes

A guest process is a host process, so a syscall that carries a PID used to act on whatever host process had that number. Inside a rootfs
a guest may now only name processes of its own tree (the launching pemu, which is the child subreaper, and everything below it, orphans
included). For any other process these answer `ESRCH`: `kill`, `tkill`, `tgkill`, `rt_sigqueueinfo`, `rt_tgsigqueueinfo`, `pidfd_open`,
`sched_*`, `getpgid`, `getsid`, `setpgid`, `prlimit64`, `capget`, `get_robust_list`, `getpriority`, `setpriority`, `ioprio_get/set`.
`kill(0, s)`, `kill(-pgrp, s)` and `kill(-1, s)` reach the processes of the tree only, so a signal to the process group no longer hits the
shell that started pemu; `setpriority(PRIO_PGRP / PRIO_USER)` is limited the same way. POSIX message queues (`mq_open`, `mq_unlink`, x86_64
guest) get a per-rootfs name prefix, so a guest and the host do not share queues. Set `PEMU_PID_FENCE=0` to turn the PID part off.
The choice is made by the first pemu and handed to the ones started by `execve` as an option: a guest cannot switch it off from its environment.

This is **not PID virtualisation**: the guest still sees host PIDs (numbers), and `/proc` shows every host process, but it cannot act on a process it did not start.

## Refused on purpose

These return `EPERM`: `ptrace`, `setns`, `unshare`, `pivot_root`, `bpf`, `reboot` and other calls that
could reach the host. Programs usually fall back to something else. `io_uring` is refused with `ENOSYS`.

## Log of what does not work (`~/.pemu/pemu.log`)

While pemu runs it writes down everything that failed or is not supported, so you can see afterwards why a program misbehaved, without
`-strace`. The file is `~/.pemu/pemu.log`, next to `~/.pemu/rootfs` (without `HOME`: `$PREFIX/var/lib/pemu/pemu.log`). It is always on:
there is no option or environment variable to switch it off or on, and it never writes to the guest's own stdout / stderr.

| kind | meaning |
|---|---|
| `unsupported-syscall` | a guest syscall pemu has no implementation for (the guest gets `ENOSYS`) |
| `enosys` | a syscall answered `ENOSYS` (declined on purpose: `rseq`, `clone3`, `io_uring` in a rootfs ...; or the host said so) |
| `refused` | a call pemu refuses on purpose: `ptrace`, namespaces, `pivot_root`, `mount` of other types than bind, `seccomp` with `NEW_LISTENER`, the PID fence ... |
| `host-seccomp` | the host's own seccomp filter (Android, containers) blocked a syscall pemu forwarded; the guest got `ENOSYS` |
| `unsupported-insn` | an instruction the interpreter does not implement (the guest gets `SIGILL`) |
| `jit-fallback` | an instruction the JIT does not translate and runs in the interpreter instead (correct, but slower) |
| `guest-fault` | a fatal fault or signal that no guest handler caught, privileged instructions (`in`/`out`/`hlt`) |
| `error` | start-up, ELF loading, `execve` and rootfs errors, and pemu's own warnings |

    2026-11-16T01:05:51Z pid=8758 [myapp] unsupported-syscall: syscall 435 (clone3) has no implementation, answered ENOSYS (a0=0x7ffd... a1=0x58)
    2026-11-16T01:05:53Z pid=8758 [myapp] unsupported-syscall: repeated x212: syscall 435 (clone3) has no implementation, ...

Times are UTC. An event that repeats is written once (per process); when the process exits one `repeated xN` line gives the count (a process
that is killed does not write it). All processes of one run append to the same file, one `write` per line. Over 8 MiB the file is renamed to
`pemu.log.1` (one older generation is kept). It is not removed by `apt remove pemu` or `pemu uninstall`.

## Calls into the host kernel (`PEMU_HOST_STATS`)

Every call that leaves the emulator for the host kernel goes through one header, `host/pemu_host.h`: `hs()` (the guest's syscalls) is `host_syscall()`,
the emulator's own memory calls are `host_mmap` / `host_munmap` / `host_mprotect` / `host_madvise`, and processes and threads are `host_fork` /
`host_waitpid` / `host_thread_create`. They do exactly what the plain calls did; the point is that the host dependency is now in one place, can be counted,
and can be replaced piece by piece. `tools/check_host_seam.py` (run by `build.sh`) fails when code outside the header calls `mmap`, `fork`, `syscall` ... directly again.

    PEMU_HOST_STATS=1 pemu prog                    # summary on stderr when the guest exits
    PEMU_HOST_STATS=/tmp/hs pemu prog              # one file per process: /tmp/hs.<pid>

    pemu host: 21 calls into the host kernel, 7 kinds
    pemu host:        1 x  write (1)
    pemu host:        1 x  close (3)
    pemu host:        8 x  mmap (9)
    pemu host:        4 x  mprotect (10)
    pemu host:        5 x  pread64 (17)
    pemu host:        1 x  openat (257)
    pemu host:        1 x  getrandom (318)

(the run above: a guest with no libc that makes one `write`; the rest is the emulator's own set-up: memory map, ELF loader, auxv random.)

The numbers are the host kernel's syscall numbers. Counted: everything through `hs()` and the `host_*` functions: syscalls, memory (`host_mmap` ... are syscalls,
no libc), and the emulator's own `open` / `read` / `write` / `pread` / `lseek` / `close` / `stat` / `lstat` / `fstat` (counted as the syscall glibc makes for them). **Not** counted yet: the
other libc calls pemu makes for itself (`getpid`, `kill`, `unlink`, `mkdir`, signals, stdio ...); they move behind the header in the next batches, so the total only goes
up as the measurement gets complete. The `exit_group` of the guest itself is not in the summary (it is printed just before). `tests/integration/host_stats.sh` checks it with a guest that has no libc and makes one `write`.

### Host operations by name (`HOP_`)

The emulator (`src/`) does not know the host's syscall numbers either. It asks for an operation by name, `hs(HOP_openat, ...)`, and `host/pemu_hop.h` (generated by `tools/gen_hop.py`, checked by `build.sh`) says what that name is on this host: on Linux the host's own syscall number, so the binary is the same as before. A host other than Linux needs to implement these operations, not to renumber the Linux layer. `tools/check_host_seam.py` fails on `SYS_<name>` or a bare number in `src/`, and `PEMU_HOP_SCRAMBLE=1 ./build.sh` builds a pemu whose `HOP_` values are not Linux numbers: `tests/integration/hop_scramble.sh` runs the syscall-layer tests on it (it found a hidden dependency when it was introduced: the `io_uring` calls were forwarded by their Linux numbers).

## x87 floating point (x86_64 guest)

Follows the hardware for:

- Exception flags (IE, DE, ZE, OE, UE, PE, stack faults, C1). An unmasked exception raises `SIGFPE` with the right `si_code` at the next waiting x87 instruction; the instruction stores nothing and pops nothing.
- Unmasked OE / UE: the rounded result with its exponent brought back by 2^-24576 / 2^24576.
- Rounding control (nearest, down, up, toward zero) for arithmetic, `fst` m32/m64, `fpatan` and the transcendentals; denormal results use the CPU's grid (depends on the precision control).
- `fsin`/`fcos`/`fptan`/`fsincos` reduce with the CPU's 66-bit pi and set C2 for |x| >= 2^63.

## Limitations

- **Not a security boundary.** `/dev`, `/proc` and `/sys` are the host's own, unfiltered, and some calls can race on symlinks.
  System V IPC (`shmget`, `semget`, `msgget`) is shared with the host.
- **Fake root is cosmetic.** Files owned by you look like root's, and `chown`/`setuid` always succeed.
- See `docs/REVIEW.md` for what is not verified.

## Build, test, source layout

    GUEST=x86_64 ./build.sh        # guest = a directory of src/guest/ (x86_64 is the default and the only one)
    ./build.sh                     # builds ./build/pemu, runs the static checks, then the whole test suite
    SKIP_TESTS=1 ./build.sh        # build and static checks only
    tests/run.sh -l                # list the tests;  tests/run.sh seccomp jit   runs the ones whose name contains a word

    src/pemu.c                     entry file (one translation unit)
    src/guest/x86_64/{cpu,linux}/  x86_64 guest: interpreter, and its Linux syscall / signal / ELF layer
    src/jit/x86_64-aarch64/        JIT for aarch64 hosts
    src/os/linux/                  parts of the Linux layer every guest shares (host syscall hook-up, signals, brk, ELF loader, execve, threads, main)
    src/proot/                     built-in proot (rootfs, bind mounts, fake root, seccomp ...), architecture neutral
    host/                          libc-free runtime, kernel ABI headers, the host seam (pemu_host.h) and the host operation names (pemu_hop.h, generated)
    tests/  tools/  docs/          tests (unit, integration, oracle), generators and checks, architecture + review notes

`docs/ARCHITECTURE.md` explains the layers and the recipes for adding an instruction, a syscall, a guest architecture or a JIT target; `tests/README.md` explains how to add a test.

## Without libc (the only build)

No libc code **and no libc header**: `host/abi/include/` holds the Linux ABI for x86_64 and aarch64 with the libc names (`pemu_abi.h`: types, struct layouts, flags, signal / errno / syscall numbers; `pemu_proto.h`: the prototypes of what `host/rt/` defines, made by `tools/genproto.py`), and the build passes `-nostdinc -I host/abi/include`. So glibc, musl and bionic need not agree on anything: struct stat, sigaction, ucontext, sigjmp_buf, FILE, ... are pemu's own definitions of the kernel's layouts. `tests/integration/nolibc_headers.sh` compiles with exactly that and checks `nm -u`; with clang present it also compiles the aarch64 host build (`src/pemu.c`) for `--target=aarch64-linux-gnu`.

`./build.sh` links `pemu` with **no libc**: `-nostdlib -static-pie`, `nm -u` is empty, `ldd` says "statically linked". `host/rt/` is the
replacement, compiled into the emulator (`host/pemu_host.h` includes it); every function keeps the libc name and prototype, so no caller changed.

| file | what it holds |
| --- | --- |
| `rt_core.h` | `_start`, self-relocation (static-pie), `.init_array`, TLS, vDSO lookup, threads (`clone`), futex mutex, `fork`, `sigaction` / `sigprocmask`, `sigsetjmp` / `siglongjmp`, `exit` |
| `rt_mem.h` | `mem*`, `str*`, `strto*`, `qsort`, `strerror`, ctype, `malloc` family (size classes over `mmap`), `getenv` / `setenv` |
| `rt_stdio.h` | the printf engine (`snprintf`, `fprintf` ...), `sscanf`, `FILE` (`fopen`, `fgets`, `fwrite` ...) |
| `rt_sys.h` | thin syscall wrappers, `opendir` / `readdir`, `realpath`, `execvp`, `clock_gettime` (vDSO), `gmtime_r` |
| `rt_math.h` | `fenv`, rounding, exact `fma` / `fmaf` / `fmal` in every rounding mode, x87 `long double` functions (aarch64: FPCR / FPSR and `rt_quad.h`) |

What is checked: the whole `build.sh` suite runs on this binary; `fma*` and the long double basics match glibc bit for bit on random inputs (all four rounding modes);
a test program built with and without libc prints identical output (printf, scanf, strings, malloc, env, files, realpath, directories, signals, threads, fork + exec).
Known limits: aarch64 hosts: `long double` is binary128 there, done by `rt_quad.h` (integer soft-float: sqrt, fma, fmod, remainder, ldexp, frexp, roundings, nextafter exact in all four rounding modes; expm1 and atan2 within a few ulp; `tests/integration/rt_quad.sh` checks it on x86_64 against libquadmath and an exact oracle), but the aarch64 asm (`rt_core.h`, FPCR / FPSR in `rt_math.h`) has never run on real hardware, so building there prints a warning;
`expm1l` differs from glibc by up to 2 ulp, but measured against the exact value (mpmath) it is closer than glibc's own (worst 1.4 ulp against glibc's 2.2 on the ranges tried), so it is not matched to glibc; `nextafterl` is bit-identical to glibc 2.39 on random inputs including pseudo-denormals and unnormals (`tests/manual/rt_math.sh`); no header of the system is read either: the NOLIBC build is compiled with `-nostdinc` and `host/abi/include/` is the whole ABI (see below).
Build flags that matter: `-falign-functions=64 -falign-jumps=16` (without them the interpreter was about 7 % slower from code layout alone).


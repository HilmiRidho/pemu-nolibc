# Architecture and how to extend it

pemu is **one translation unit**: `src/pemu.c` includes the parts (`*.inc.h`) in dependency order, through one manifest (`all.inc.h`) per part.
There is no libc: `host/` holds the runtime and the kernel ABI headers (see the README, "Without libc").

## Map

```
src/pemu.c                         entry file; includes the manifests below
src/guest/<arch>/cpu/              guest CPU core = the interpreter                       (arch specific)   manifest: all.inc.h
src/guest/<arch>/linux/            Linux ABI layer of that guest: syscalls, signals,     (arch specific)   manifest: all.inc.h
                                   ELF loader, main
src/jit/<guest>-<host>/            JIT back end for one guest/host pair (only HAVE_JIT)  (pair specific)   manifest: all.inc.h
src/os/linux/                      the Linux layer every 64-bit guest shares: (arch neutral)    included by guest/<arch>/linux/
                                   hook-up, signal handlers, brk, ELF loader,
                                   execve, threads / clone, /proc/self, main
src/proot/                         built-in proot: rootfs, bind mounts, fake root,       (arch neutral)    pemu_root.h
                                   seccomp, PID fence, log ...
host/pemu_host.h                   the host seam: every call that leaves for the kernel
host/pemu_hop.h                    GENERATED (tools/gen_hop.py): the HOP_<name> host operations src/ uses and what number each has on this host
host/rt/                           libc-free runtime (start-up, malloc, stdio, math ...)
host/abi/include/                  kernel ABI with libc names (-nostdinc; x86_64 and aarch64)
tools/                             generators and static checks (genproto, gen_sysnames, check_host_seam)
tests/                             see tests/README.md
packaging/  .github/              .deb packaging and CI
docs/REVIEW.md                     history of the review rounds (what was found, what was fixed)
```

Today there is one guest (`x86_64`), one JIT pair (`x86_64-aarch64`) and two hosts (`x86_64`: interpreter only, `aarch64`: interpreter + JIT).

### Dependency rules (checked by the build)

| rule | checked by |
|---|---|
| the CPU core and the JIT do not call into `guest/*/linux/`, `src/os/` or `src/proot/`; they reach the OS through the `PemuOS` table in `cpu.inc.h` | `tests/unit/check_core.c` (compiled by `build.sh` with implicit declarations as errors) |
| nothing under `src/` calls the kernel directly (`mmap`, `fork`, `open`, `syscall` ...); it goes through `host/pemu_host.h` | `tools/check_host_seam.py` |
| `src/` never names a Linux syscall number: no `SYS_openat`, no literal number as the operation; it asks for `HOP_openat`. Only `host/` turns that name into a number | `tools/check_host_seam.py` (static), `tests/integration/hop_scramble.sh` (a build whose `HOP_` values are not Linux numbers runs the syscall-layer tests) |
| `host/pemu_hop.h` is current (it lists every `HOP_` name used under `src/`) | `tools/gen_hop.py --check`, run by `build.sh` |
| no libc symbol and no system header | `tests/integration/nolibc_headers.sh` |
| `host/abi/include` has the same layouts as the system's headers | `tests/integration/abi_layout.sh` |
| `src/` does not include anything from `tests/` | (convention: the JIT profiler lives in `src/jit/x86_64-aarch64/prof.inc.h`) |

## Include order (inside a manifest)

Each part may use what the parts above it define; a manifest lists them in that order. `static` functions and macros are shared, so an
`#include` that moves up can break a part that used something from below: build after every move.

## Recipes

### Add or upgrade an instruction (x86_64 guest)

1. Interpreter: the decoder / handler in `src/guest/x86_64/cpu/` (`decode.inc.h` for the opcode map, `sse2`, `mmx`, `ssse3_sse4`, `x87` for the groups).
2. JIT (optional, the interpreter is the fallback): `src/jit/x86_64-aarch64/decode.inc.h` (static decode) and `codegen.inc.h`. An instruction the JIT does not know runs in the interpreter (`jit-fallback` in `~/.pemu/pemu.log`).
3. A guest in `tests/guests/` and a case in `tests/integration/` (see tests/README.md); JIT output is run on the oracle in `tests/oracle/aarch64/`.

### Add a syscall or refuse one

- The host call itself is `hs(HOP_<name>, ...)` (or `pemu_call*`, `host_syscall_e`): name the host operation after the Linux syscall, never `SYS_<name>` and never a bare number. Run `python3 tools/gen_hop.py` to add the name to `host/pemu_hop.h` (`build.sh` fails when it is stale). A syscall that old libc headers lack, and whose number is the same on every architecture, goes into `FALLBACK` in `tools/gen_hop.py`.
- Arch-neutral behaviour (paths, rootfs, ids, sockets): `src/proot/`. Refusals: the one table in `src/proot/deny.inc.h`.
- The guest's syscall numbers and argument conventions: `src/guest/x86_64/linux/syscall.inc.h`. `sysnames.inc.h` is generated: `python3 tools/gen_sysnames.py`.
- Unit test in `tests/unit/`, end-to-end in `tests/integration/` with a guest in `tests/guests/`.

### Add a guest architecture (for example riscv64)

1. `src/guest/riscv64/cpu/` with an `all.inc.h` (state struct, decoder, interpreter) and `src/guest/riscv64/linux/` with an `all.inc.h` (syscall table, signal frame, ELF `e_machine` check, `main`).
2. `src/proot/`, `src/os/linux/` and `host/` are reused as they are (`src/os/linux/` = the shared Linux layer; include them from the new `linux/` layer as `guest/x86_64/linux/` does).
   Everything generic is already in `src/os/linux/`: host syscall hook-up, signal state and host handlers, `brk`, ELF loader, `execve`, threads / `clone` / `clone3`, the `/proc/self` fakes and `main`.
   The new guest supplies in `linux/`: `guest.inc.h` (the names the shared files list in their header comments: `PEMU_ELF_MACHINE`, `GUEST_HWCAP`, `GUEST_PC`, `guest_cpu_start()`, `guest_set_sp()` ...),
   `sigframe.inc.h` (signal frame), `abi.inc.h` (open flags, `stat` layout), `sysnum.inc.h` + `syscall.inc.h` (syscall numbers and the dispatch `switch`), `procfake.inc.h` (`/proc/cpuinfo`, legacy syscalls), `cpu_loop.inc.h`, and an `all.inc.h` with the include order of `guest/x86_64/linux/all.inc.h`.
   The dispatch itself (`gsys` in `syscall.inc.h`) is x86_64 by nature (its `case` labels are x86_64 numbers); parts of it that turn out to be identical for the new guest can be moved to `src/os/linux/` then, shaped by two users.
3. Select it in `src/pemu.c` (the `PEMU_GUEST_<arch>` switch: add a branch and an `#include` of the new manifests); `./build.sh` takes `GUEST=<arch>` (a directory of `src/guest/`). `tests/unit/check_core.c` still includes the x86_64 core directly.
4. Add the guest's tests: `tests/guests/`, `tests/integration/`, and extend `tests/unit/check_core.c` to include the new core.

### Host operations (`HOP_`): what a new host backend implements

`src/` asks the host for work with `hs(HOP_x, a0 .. a5)`: the name of an operation, arguments in the Linux ABI (Linux flag values, struct layouts and pointers: that is the Linux layer's contract, kept as it is). On the Linux backend `HOP_x` is the host's own syscall number, so nothing changes. A different host (BSD, macOS, Windows, a bare-metal runtime) provides its own meaning for the same names: give `HOP_NR()` / `host_syscall()` in `host/pemu_host.h` a body that implements each operation with the host's API and answers `-ENOSYS` for the ones it cannot do; the list of names is `host/pemu_hop.h`. `PEMU_HOP_SCRAMBLE=1 ./build.sh` is the proof that `src/` does not depend on the numbers: it gives every `HOP_` name a value that is not a Linux number.

### Add a JIT pair or a host

- A new host with the same guest: a sibling directory `src/jit/x86_64-<host>/` with its own `all.inc.h` (encoders in `emit.inc.h`, register map in `tcg_target.inc.h`); `decode.inc.h` (guest side) can be copied or, better, moved up into `src/guest/x86_64/jit/` once two pairs need it.
  The `HAVE_JIT` switch in `cpu.inc.h` chooses it. Test it the way the aarch64 one is tested: add `tests/oracle/<host>/` that runs the emitted code on an interpreter.
- A new host runtime (not a new JIT): `host/rt/rt_core.h` holds the per-architecture asm (`_start`, `clone`, signal return); add the `#elif defined(__<arch>__)` branches and the matching ABI in `host/abi/include/pemu_abi.h`.

### Upgrade the toolchain or CI

- Compiler flags and build modes: `build.sh` (one place; every flag is probed). Packaging: `packaging/build-deb.sh`. CI: `.github/workflows/build-deb.yml`.
- Version: the `VERSION` file is the single source (compiled in as `PEMU_RELEASE`, used by the CI for package names).

# Review of the syscall translation layer

Scope: `src/proot/pemu_root.h` (parts in `src/proot/`) and the syscall hook (`pemu_sys` in `src/guest/x86_64/linux/syscall.inc.h`).

> Note: this is a history of review rounds. AArch64 guest support has since been removed, and the binary `pemu_x64` of the older rounds is now simply `pemu`
> (entry file `src/pemu.c`; sources under `src/`). Mentions of the AArch64 guest, `pemu_a64_sys` or `linux-user/aarch64/` below describe the code as it was then.
> The AArch64 interpreter survives only as a test oracle for the JIT (`tests/oracle/aarch64/`).
>
> Names in the rounds below, and what they are today (the text of the rounds is left as it was written):
>
> | In the text | Today |
> |---|---|
> | `pemu_x64` (binary), `pemu_x64.c` | `pemu`, `src/pemu.c` |
> | `pemu` as the AArch64-guest binary, `pemu_a64_sys`, `linux-user/aarch64/` | removed (only the oracle in `tests/oracle/aarch64/` remains, for the JIT tests) |
> | `linux-user/` | `src/guest/x86_64/linux/` and `src/proot/` |
> | `src/cpu/`, `src/linux/`, `src/jit/` (older rounds) | `src/guest/x86_64/cpu/`, `src/guest/x86_64/linux/`, `src/jit/x86_64-aarch64/` |
> | `tests/<name>.sh` | `tests/integration/<name>.sh` |
> | `tests/test_*.c` | `tests/unit/test_*.c` |
>
> Statements that no longer hold: "AArch64 under `pemu`" and "The AArch64 guest ... was not changed" (guest removed); "AArch64 hook had the same code" / "AArch64 got `openat2`" (there is no AArch64 hook any more); the Round 2 remark that the aarch64-host
> build was not compiled and the mention of `NOLIBC=` / `STATIC=` (the build has one mode, see the last section).

## Fixed in this revision

| # | Finding | Fix |
|---|---|---|
| 1 | **Symlink race on open.** The guest walk validated a path, then the host kernel walked the resulting host path again; a guest thread could swap a directory for a symlink in between and escape the rootfs. | Opens inside the rootfs now end in `openat2(rootfd, path, RESOLVE_IN_ROOT)`. Falls back to `openat` without `openat2` (Linux < 5.6, Android by default). |
| 2 | **`openat2` flags dropped** (x86_64 hook zeroed `how.resolve`); the AArch64 hook had no `openat2` at all (ENOSYS). | `RESOLVE_NO_SYMLINKS/BENEATH/IN_ROOT` enforced in `pemu_canon`; `NO_MAGICLINKS/CACHED` passed through; unknown bits give `EINVAL`; AArch64 got `openat2`. |
| 3 | **AF_UNIX `sendto`/`sendmsg`** passed guest paths to the host, so datagram sends to a path inside the rootfs failed or hit the wrong file. | Translated, plus `sendmmsg`. |
| 4 | **`getsockname`/`getpeername`/`recvfrom`/`recvmsg`/`accept` returned host paths** to the guest (leaks the rootfs location, breaks programs that compare or reuse the name). | Returned addresses are mapped back to guest paths, honouring the guest buffer size. |
| 5 | **`bind`/`connect` failed with ENAMETOOLONG** whenever the host path exceeded 108 bytes, which is common for rootfs under `/data/data/com.termux/...`. | Long paths go through `/proc/self/fd/<dirfd>/<name>`. |
| 6 | **Shell injection in `pemu install`:** tarball and destination were interpolated into `system()`/`popen()` strings inside single quotes, so a quote in the path broke out. | `fork`/`execvp` with plain arguments; `mkdir -p` done in C. |
| 7 | `fchmodat2` (glibc 2.39+ for `AT_SYMLINK_NOFOLLOW`) was not translated. | Added to both hooks. |
| 8 | Android hides `/proc/stat`, `loadavg`, ... | Fake files served from a memfd when the real open fails. |
| 9 | `-Wformat-truncation` warnings in `pemu_canon` / `pemu_install`. | Replaced `snprintf` with bounded `memcpy`. |
| 10 | **Races on the mutating path syscalls** (mkdir, unlink, rename, link, symlink, mknod, chmod, chown, utimes, truncate, xattr, chdir). | The parent directory (or the target, when the last component is followed) is opened with `openat2(RESOLVE_IN_ROOT)` and the call uses `/proc/self/fd/N[/name]`. A stress test swaps a directory and a symlink in a loop: 6944 escapes with the plain walk, 0 with this one. |
| 11 | **A `#!` script reached through a symlink ran under the wrong name.** `pemu_exec_resolve` handed the interpreter the symlink's target, the kernel hands it the path as given. `addgroup` (a symlink to `adduser`) therefore behaved as `adduser` and created a user instead of a group. | The interpreter gets the path as the guest gave it; only a path rebuilt from an fd uses the canonical name. Regression test with a symlinked script. |
| 12 | **`RESOLVE_NO_XDEV` was accepted but not enforced.** | Enforced in `pemu_canon`: stepping between two entries of the bind table (into a bind, onto its mount point, out of it through `..`) fails with `EXDEV`. Tests cover binds, `..`, symlinks, `/proc` and `openat2`. |
| 13 | **`execveat` / `execve("/proc/self/fd/N")` of a file with no guest name** (memfd, file deleted after open) died after the exec (exit 127), because the new image tried to open the name or the close-on-exec fd. | The fd is handed to the new image (`--exec-fd`, hidden), which loads from it and closes it; a nameless `#!` script returns `ENOENT`. AArch64 gained `execveat` (it was `ENOSYS`). Checked with a static x86_64 guest: memfd, deleted, named, with and without a rootfs, no leaked fd. |
| 14 | `pivot_root`, `acct`, `reboot`, `swapon/off`, `kexec_*` and the module calls were `EPERM` on x86_64 but `ENOSYS` on AArch64. | Both hooks answer `EPERM`. |

## Round 2

Found by reading the code against the claims of this file and of the README.  Items 15-20 have a test in `tests/unit/test_pemu_fixes.c` (it once also
built against the previous code, to show the tests fail there); items 21-23 in `tests/integration/uring.sh` and `tests/integration/ns.sh`, items 24-26 in `tests/integration/sock.sh` (real x86_64 guests on an
x86_64 host).  Every test fails on the previous code except the `open_priv` race of item 19, which could not be reproduced.

| # | Finding | Fix |
|---|---|---|
| 15 | **The "openat2 off on Android" default never applied through `pemu login`.** `pemu_secure()` decides lazily from `ANDROID_ROOT`; `login` calls `clearenv()` (which drops it) before the first guest open, so on Termux the race-free path was switched **on**, where the seccomp filter may reject `openat2` and the fallback only handles `ENOSYS`. A guest running with a scrubbed environment did the same in every re-exec'd child. | The decision is taken in `pemu_login` before `clearenv()` and passed to children as the hidden `--secure-open 0/1`. Test fails on the old code (got 1). |
| 16 | **Host `O_TMPFILE` hard-coded as `020200000`** in `pemu_open`. That is the x86 value; on aarch64 hosts it means `O_TMPFILE`\|`O_DIRECT`. Worse, on x86 it also contains `O_DIRECTORY`, so *any* `O_DIRECTORY` open with a non-zero mode register (raw `openat` callers) went to `openat2` with a mode and failed with `EINVAL` (reproduced). The `NO_MAGICLINKS` fallback passed the mode unconditionally. | `PEMU_OPEN_HAS_MODE()` uses the host `O_CREAT` / `O_TMPFILE`. A guest `openat2` with a mode but no create flag is `EINVAL`, as in the kernel. |
| 17 | **Bind table edited under readers**: `chroot` replaced `g`/`gl` and `h`/`hl` in separate steps and `pemu_root` with `snprintf`, while other threads walked the table without a lock. `umount` only marked slots `off`; they were never reused, so 64 mounts over the life of a process ended in `ENOSPC` (reproduced at mount #61). | Entries are fixed buffers guarded by a seqlock (readers retry, writers hold `pemu_lock`); a freed slot is reused and `ord` keeps "later mount wins"; `umount` removes the newest mount of a path first; re-exec passes binds oldest first. `pemu_atfork_child()` releases a lock or half-done edit inherited by a fork child (both hooks; AArch64 had no such reset). |
| 18 | **`RESOLVE_CACHED` was accepted and dropped**, although the README said it goes to the host. | Forwarded on both open paths. See "Still open" for its limit. |
| 19 | **`pemu_open_priv` (fake root) chmod'ed host files by path, unserialised.** A swap of the path between the check and the restore could redirect the chmod; two threads could mistake the temporary mode for the real one. The README called fake root "cosmetic". | Inode pinned with an `O_PATH` fd, chmod / open / restore all through `/proc/self/fd/N`, threads serialised, `O_CREAT`/`O_EXCL`/`O_NOFOLLOW` dropped for the re-open. README text corrected. The race itself could **not** be reproduced on the old code (8 threads x 20000 opens), so this is a hardening, not a proven bug. |
| 20 | AArch64 hook used `pemu_force` for `fchown`, `capset`, `setgroups`; the x86_64 hook uses `pemu_force_acc` / `pemu_force_ns` for the same calls. | Same wrappers in both hooks. |
| 21 | **`io_uring` bypassed the rootfs** (x86_64 guest). `io_uring_setup/enter/register` were passed through; an `IORING_OP_OPENAT` / `STATX` / `UNLINKAT` / `RENAMEAT` / `MKDIRAT` / `SYMLINKAT` / `LINKAT` carries a guest path that the *kernel* resolves later from the ring, so pemu never translated it. Reproduced with a static guest: `open("/p")` read the rootfs file, an io_uring OPENAT of the same path read the **host** file. The AArch64 hook (a whitelist without io_uring) was not affected. | Inside a rootfs (`-r` / `login`) all three answer `ENOSYS`, which libuv, tokio, liburing users and glibc fall back from. Without a rootfs there is nothing to bypass and the rings are passed through as before. `tests/integration/uring.sh`. |
| 22 | **Namespace calls acted on the host or lied.** `clone(CLONE_NEWUSER\|...)` (and the AArch64 `clone`) ignored the flags and returned a plain fork, so a sandbox tool (bwrap, chrome) believed it was isolated; `clone3` already refused them. `unshare` and `setns` went to the host kernel and changed the emulator process itself. | `clone` with a `CLONE_NEW*` flag is `EPERM` in both hooks (as `clone3`); `unshare` with such a flag and `setns` are `EPERM`. Later unified: one table for both hooks (`src/proot/deny.inc.h`), also without a rootfs. `tests/integration/ns.sh`, `tests/integration/deny.sh`. |
| 23 | **`name_to_handle_at` took the path as a host path** (x86_64 whitelist); `open_by_handle_at` is the classic "shocker" container escape when pemu runs as real root. | `name_to_handle_at` goes through the path translation (the x86_64 hook used the literal `303`, wrong on an aarch64 host; now `SYS_name_to_handle_at`, and the AArch64 hook translates it too); `open_by_handle_at` is `EPERM`. `tests/integration/ns.sh`, `tests/integration/deny.sh`. |
| 24 | **`connect()` / `bind()` / `sendto()` on an AF_UNIX path the guest lookup refuses (symlink loop, name too long) went to the host kernel with the raw GUEST path**: `pemu_unix_prep` treated every error of the translation as "leave it alone". Reproduced: in a rootfs where `/loop` is a symlink to itself, `open()` said `ELOOP` and `connect()` reached the host's socket at the same path. | The error is returned; only "not a rootfs path" (raw `/proc /sys /dev` binds) still passes the path on. `tests/integration/sock.sh`. |
| 25 | **`connect()` / `sendto()` to a symlink with an absolute target failed with `ENOENT`** (`/link -> /run/real.sock`): the lookup did not follow the last component, so the host kernel followed the link against the host root. | `connect` / `sendto` / `sendmsg` / `sendmmsg` resolve the path with a followed last component; `bind` still does not follow. `tests/integration/sock.sh`. |
| 26 | **`recvmmsg` returned host paths as the sender's name** (`recvfrom`, `recvmsg`, `accept`, `getsockname` were converted, this one was a plain pass-through). | Hooked in both guests (x86_64 299, AArch64 243), every received message converted. `tests/integration/sock.sh`. |

## Round 3

| # | Finding | Fix |
|---|---|---|
| 27 | **A mutating call on a direct child of the rootfs root used the wrong name.** In `pemu_path_s` the last component was taken from the buffer that was cut one line later, so for a parent of `/` the name was empty and the host path became `/proc/self/fd/N/`, the root itself: `mkdir("/d")` and `symlink(_, "/l")` failed with `EEXIST`, and `lchown("/l")` changed the owner of the **rootfs root directory**. Found while testing item 28 (owner of the rootfs root turned up as 5:5); the same code ran before. Only the race-free walk is affected (off on Android by default). | The name is taken from the untouched canonical path. `t_top_level` in `tests/unit/test_pemu_fixes.c` fails on the previous code (4 checks) and `tests/integration/owner.sh` covers it with a real guest. |
| 28 | **`chown` of a fake root was reported as done but never recorded** (`pemu_force` turned the host's `EPERM` into success; `stat` then showed root again). | Fake owners: uid/gid kept in the xattr `user.pemu.own`, shown by `stat`/`fstat`/`statx`; see README "Fake owners" for what it does not cover. `src/proot/fake_owner.inc.h`; checks in `tests/unit/test_pemu_fixes.c` (`t_fake_owner`) and `tests/integration/owner.sh` (x86_64 guest; as an unprivileged host user the old code answers 0:0 after `chown 1000:100`). |

Item 28 is deliberately small: report-only (no permission check uses the ids), uid/gid only, best effort (any xattr failure keeps the old
behaviour), active only for a fake root inside a rootfs, `PEMU_FAKE_OWNER=0` turns it off. Modes (`chmod`), `mknod` and capabilities are not
recorded yet; `pemu_open_priv` is unchanged.

## Still open

* **Races on read-only path syscalls** (stat, access, readlink, getxattr, statfs) remain by design: they stay on the fast path. `execve` also resolves its file separately.
* `pemu_fchdir`: if the fd points outside every bind, the host cwd changes but the tracked guest cwd stays stale.
* `getsockname` on a socket bound through the long-path trick reports the `/proc/self/fd/N/...` form (the kernel stores that string).
* Raw binds (`/dev /proc /sys`) are plain pass-through: resolved by the host kernel by design, with no allowlist and no read-only protection. Exception: `/proc/mounts` and `/proc/<pid>/mounts|mountinfo` are rebuilt for the guest (`fake_proc.inc.h`; test `t_mounts`). `/proc/<pid>/mountstats` still shows the host. In mountinfo every parent id is 1 and every root field is `/`.
* `pivot_root` is refused on purpose (`EPERM`): it needs a private mount namespace and real mount types, and pemu only edits a bind table. `unshare` with a `CLONE_NEW*` flag, `setns`, `ptrace`, `open_by_handle_at` and the new mount API are refused with `EPERM` on both architectures by one table (item 22).
* `RESOLVE_NO_XDEV` sees pemu's mounts (the bind table), not host mounts inside the rootfs directory.
* `RESOLVE_CACHED`: pemu's own walk `lstat`s every component first, so the dentries are cached by the time `openat2` runs and the kernel
  practically never answers `EAGAIN`. The flag is validated (`EINVAL` on kernels before 5.12) but its "do not block" meaning is lost.
* Another process can see the temporary mode bits of `pemu_open_priv` for the length of one open; a `kill -9` of pemu in that moment
  leaves them set.
* The x86_64 hook is a whitelist of pass-through syscalls (`pass_nr`, `pass_nr2`); every entry that takes a path, a handle or a namespace is a
  potential bypass of the rootfs and was only checked for the ones listed above.  Entries not looked at in detail: `mq_open` / `mq_unlink` (the
  host's message-queue namespace is shared), `io_submit` (fd based, believed fine).  `process_vm_readv/writev`, `kcmp`, `pidfd_getfd`, `bpf`, `perf_event_open`, `userfaultfd`, `landlock_*`, `statmount` / `listmount` the key calls, the clock / hostname / syslog calls and `migrate_pages` / `move_pages` / `process_madvise` are now refused by `src/proot/deny.inc.h` (`tests/integration/deny.sh`).
* `bind` / `connect` still resolve the path with the plain walk, not the race-free one: a guest thread that swaps a parent directory for a
  symlink at the right moment could make `bind` create the socket outside the rootfs.  Using the parent-directory fd (as `mkdir` does) would
  leave `/proc/self/fd/N/name` as the socket's name, which `getsockname` and datagram senders would then show to every program, so it was
  not done.
* The fallback for a missing `openat2` only reacts to `ENOSYS`. A seccomp filter that answers `EPERM` or kills the process is not
  detected; on Android the path stays off by default (item 15) and `PEMU_SECURE_OPEN=0` forces it.

## Round 5: Electron / Chromium (x86_64 guest)

Goal: make Electron apps start under `pemu_x64`.  Found by running Node.js (a V8 / libuv program, the closest x86_64 binary
available on the build machine) under the emulator and reading what Chromium does at start-up.

| # | Finding | Fix |
|---|---|---|
| 29 | **`/proc/self/exe` named the emulator.** Without a rootfs `readlink("/proc/self/exe")` returned the path of `pemu_x64`, so Electron looked for `icudtl.dat`, `*.pak` and `resources/` next to the emulator.  With or without a rootfs, `execve("/proc/self/exe")` (how Chromium starts renderer, GPU and utility processes) re-ran the emulator with no program (`pemu: unknown option ...`).  `open` / `stat` of it saw the emulator's ELF file. | The path of the program is kept in `pemu_exe` (a host path without a rootfs).  `readlink` answers it; `open`, `stat`, `statx`, `access` and `execve` / `execveat` of `/proc/self/exe` and `/proc/<pid>/exe` are redirected to it before the normal translation.  Not done for a program handed over by `--exec-fd` (memfd, deleted file): it has no name.  `tests/integration/selfexe.sh` fails on the previous code (exec status 127). |
| 30 | **`/proc/self/cmdline` showed the emulator's command line.** | Served from the guest's `argv` strings on its stack (so a `setproctitle` is visible, as in the kernel). |
| 31 | **`prctl(PR_SET_SECCOMP)` went to the host.** A guest filter holds x86_64 syscall numbers; installed on the host process (another ABI) it kills the emulator at its next syscall.  `PR_GET_SECCOMP` showed the host's mode (2 inside a container or under Android). | `PR_SET_SECCOMP` is `EINVAL`, `PR_GET_SECCOMP` is 0, matching `seccomp()` which already answered `ENOSYS`. *Superseded: the guest's filter is now run by the emulator (proot/seccomp.inc.h), see README "Guest seccomp"; the invariant stays: it never reaches the host process.* |

Run under the emulator and found working (no change needed): WebAssembly (including the out-of-bounds trap, which V8 implements with a
`SIGSEGV` handler that rewrites the context), stack-overflow `RangeError`, 1 GiB `ArrayBuffer` / wasm memory, worker threads,
`fork` + `socketpair` + `SCM_RIGHTS` handle passing, inotify, child processes, crypto, zlib, Intl.

### Round 8: performance under Wine (x86_64 guest on an aarch64 host)

Wine maps, re-protects and signals constantly. Three costs that did not depend on what the guest computes were found by reading the code and measured or tested on an
x86_64 host (the AArch64 JIT itself ran in the embedded interpreter, so **nothing here was timed on real aarch64 hardware**).

| # | Finding | Fix |
|---|---|---|
| 37 | **`pemu_pid_owned` read `/proc/<pid>/stat` (open, read, close, sscanf) for every ancestor on every `tgkill`, `kill`, `tkill`, `sched_*`, `getpgid`...**; `tgkill` checks two ids. Wine's `pthread_kill` (thread suspend / abort), wineserver's signals to its clients and thread-priority changes are the usual callers (inferred from Wine's design, not traced in a real run). Measured: 5.8 us per `tgkill(self)`, 5.7 us per `kill(child,0)`, 5.6 us per `sched_getaffinity(child)` (native: 0.2-0.5 us). | The process itself is in the tree by definition (no /proc); `tgkill(own tgid, tid)` needs no check of the tid (the kernel refuses a tid of another process with ESRCH); an id found in the tree is remembered for 250 ms (positives only, one 64-bit word per slot so a reader never pairs a pid with another entry's expiry). After: 0.5-0.6 us. A pid cannot come back for a different process within 250 ms (pids are handed out round-robin), and the old check already had a check-then-use race. `tests/integration/pidfence.sh` passes unchanged. |
| 38 | **`mprotect` flushed the whole JIT cache whenever the range held translated code, even for r-x over r-x.** A flush throws away every translation (all threads stop, the code is translated again from scratch). | `mprotect` flushes only when the pages stop being executable, or become writable and executable (`jit_note_rwx`). r-x over r-x cannot change code that was translated as r-x: writing it needs a write permission first, and giving that up again is the flushing case. `tests/integration/jit_mprotect.sh`: 23 flushes before, 3 after (the 3 that are needed), results equal to a native run. Known limit, unchanged: code changed through a second shared mapping of the same memory is not noticed (it was only noticed by accident when the guest also called `mprotect`). |
| 39 | **`jit_post_syscall` took the translators' lock `jmu` on every `mmap`/`mprotect`/`munmap`/`mremap`, and `jit_has_code` read all of `jpg` (256 KB) for any range of 64 pages or more.** Wine reserves and releases very large ranges. | Lock-free pre-check against the page range that holds translated code (`jpg_lo`/`jpg_hi`): most calls return without the lock and without a lookup. Ranges are clipped to that window and probed page by page up to 4096 pages before the full scan is used. |

Not done, on purpose: a cache of guest path resolutions (`pemu_canon` does one `lstat` per component: about 3.4 us per `stat` and 4.8 us per `open` of a 6-component path in a
rootfs, against 0.45 / 0.75 us natively). Any such cache can be stale when another process (wineserver, another Wine process, `apt`) renames or replaces a directory,
and symlink races are exactly what rounds 1-3 closed. Persisting translated code between runs would be the largest win for Wine's many short-lived processes, but it needs
position-independent blocks and a validity check on the guest image; it is a separate project.

## Not verified here (round 5)

* **No Electron binary was available** (no network on the build machine).  Everything above is checked with Node.js and static test guests,
  not with Chromium.  Chromium may still hit an instruction or syscall that pemu lacks; run it with `-strace` (`PEMU_STRACE=1`) and look
  for `unsupported syscall` / `unsupported instruction`.
* The CPU model is unchanged (x86-64-v2, no AVX).  Chromium and its libraries select code paths with `cpuid`, so this should be fine,
  but it is untested with Chromium.
* Chromium's sandbox cannot run (no user namespaces; its seccomp layer would work, see README "Guest seccomp"): `--no-sandbox` is required.
* A host with a 39-bit address space (some Android kernels) may be too small for the address-space reservations of PartitionAlloc / V8.
* Built and tested on an x86_64 host only (the interpreter; the JIT was compiled through `PEMU_JIT_EMBED`, not run on aarch64).
* The AArch64 guest (`pemu` on x86_64) was not changed.

## Round 6: pass-through syscalls that carry a PID or a name

Found by reading the pass-through lists (`pass_nr`, `pass_nr2`, the AArch64 `init_tbl`) for entries whose argument is not an fd or a plain
number.  Tests: `tests/integration/pidfence.sh` (x86_64 guest, a real "victim" process on the host, run in its own session).

| # | Finding | Fix |
|---|---|---|
| 32 | **A PID argument reached the host unchanged.** `kill`, `tkill`, `tgkill`, `rt_(tg)sigqueueinfo`, `pidfd_open`, `sched_*`, `getpgid`, `getsid`, `setpgid`, `prlimit64`, `capget`, `get_robust_list`, `getpriority`/`setpriority`, `ioprio_*` acted on any process of the host user.  Worse, `kill(0, s)` hit the guest's whole process group, which includes the shell that started pemu, `kill(-1, s)` every process of the user, and `setpriority(PRIO_USER)` re-niced them. | `src/proot/pidfence.inc.h`: one table for both ABIs; a PID outside the tree of the launching pemu is `ESRCH`; the group / broadcast forms are expanded to the processes of the tree (the monitor, which forwards signals, is left out). Inside a rootfs; `PEMU_PID_FENCE=0` turns it off. |
| 33 | **The decision could be undone from inside the guest.** An environment switch is under the guest's control: a guest could set it before an `execve`. | The first pemu decides; re-exec'd ones get the hidden `--fence-root PID` (like `--secure-open`).  `tests/integration/pidfence.sh` sets `PEMU_PID_FENCE=0` in the guest and execs: the fence stays. |
| 34 | **`mq_open` / `mq_unlink` shared the host's queue namespace** (x86_64 hook; the AArch64 hook answers `ENOSYS`). | Inside a rootfs the name gets the prefix `pemu<hash of the rootfs>.`; verified that the host does not see the guest's queue. |
| 35 | **A plain `-static` build segfaults.** The image sits at 0x400000, where an x86_64 guest is mapped 1:1. | `STATIC=1 ./build.sh` links `-static-pie` (the whole test suite passes on it). Not the default: the dynamic build is unchanged. |

Not done (see README, "Limitations"): the guest still **sees** host PIDs and the host `/proc`; System V IPC keys (`shmget`, `semget`, `msgget`)
are host-global; `waitid` / `wait4` need no check (a non-child gives `ECHILD`).  A pid that is reused between the check and the call is not
covered (the kernel offers no atomic form short of a pidfd).  With `hidepid` or an Android `/proc` that hides other tasks the fence cannot
read parent links: pemu checks its own entry at start and turns the fence off with a warning instead of refusing everything.
`ESRCH` is returned where a real kernel would say `EPERM` for another user's process.

## Round 7: `brk` and the heap (both guests)

Found by building Wine in an x86_64 Debian guest (GCC 14) under `pemu_x64`: `cc1` died with `internal compiler error: Segmentation fault`
in `lra_remat()` -> `htab_delete` -> `__libc_free`.  Interpreter and JIT translate the instructions right (the same compile is byte for
byte identical to a native one); the defect is in the syscall layer.

| # | Finding | Fix |
|---|---|---|
| 36 | **`brk` never gave pages back.** The heap is one 1 GiB anonymous mapping, always readable and writable; `brk` only moved `brk_cur`. A real kernel unmaps the pages above the new break on a shrink, so they read as zero when the break grows again. glibc relies on this: `calloc()` does not clear memory that `malloc` just obtained from `sbrk()`. Every large `free()` of the top chunk makes glibc trim the break, so a later `calloc()` returned the old contents. GCC's hash tables (`htab_create` / `xcalloc`) then started with garbage entries and `free()` crashed. Reproduced with a 20-line guest (`tests/guests/brk_guest.c`): 32 non-zero blocks per round under `pemu_x64`, none natively. The AArch64 hook had the same code. | `src/guest/x86_64/linux/brk.inc.h` (`pemu_brk`): on a shrink the pages from `PAGE_ALIGN(new)` to `PAGE_ALIGN(old)` are zeroed (`madvise(MADV_DONTNEED)` for whole host pages, `memset` for the edges, so a host with 16 KiB / 64 KiB pages is covered too). `tests/integration/brk.sh` fails on the previous code and passes now, on the interpreter and on the JIT (embedded). |

Not verified here: a real aarch64 host.  The interpreter and the AArch64 JIT (run by the embedded interpreter) were checked; the full Wine build was not.
If the build still fails on a host with 16 KiB / 64 KiB pages (some Android kernels, Asahi), check next that `mmap` / `munmap` / `mprotect` of 4 KiB-granular
guest ranges are not rounded to the host page by the kernel; this is not handled by pemu yet (glibc's non-main arenas and `mprotect`-based heaps use 4 KiB steps).

## Not verified here

* Round 3: built and tested on an x86_64 host only (gcc), as root and as an unprivileged user (`setpriv`). The aarch64 JIT side was not
  compiled; nothing there was changed. Fake owners were not run on Termux / Android (user xattrs under SELinux, tmpfs before Linux 6.6).

* A full distribution rootfs: only a small freestanding guest (`tests/integration/e2e.sh`) has been run, as AArch64 under `pemu` and as x86_64 under `pemu_x64`.
* The Termux / bionic build and `openat2` under Android's seccomp filter.
* Round 2 was built and tested on an x86_64 host only (gcc; also with ASan/UBSan). The aarch64-host build (`pemu_x64`) was not compiled
  here; item 16 for that host follows from the header values, not from a run. The end-to-end test was skipped (no clang/lld).

### Audit of fake root and the loaders (round 4)
* `fake_id`: `setgid` decided "privileged" from the effective **gid** (`id[E] == 0`) instead of the effective uid, so a fake root with a non-zero egid only changed two of the four gids and a non-root with egid 0 changed all of them; `setuid` / `setgid(-1)` succeeded (kernel: `EINVAL`); an unprivileged `setuid(effective)` was allowed; `setreuid(real_or_effective, saved)` was refused although the kernel allows it; `setreuid(r, -1)` did not copy effective to saved; `setresuid` did not reset the fs id; `setfsuid(-1)` stored -1; `getresuid` with a bad pointer crashed the emulator instead of `EFAULT`.  `tests/unit/test_proot_compat.c` (`ids_kernel`).
* ELF loaders (both guests): the program headers were trusted.  A file size larger than the memory size wrote past the mapping, a wrapped `p_vaddr + p_memsz` gave a bogus `mmap`, a file with no `PT_LOAD` was "loaded" and jumped to, `e_phentsize` and `e_type` were not checked, and error paths leaked the fd and the header copy.  `src/guest/x86_64/linux/elf_check.inc.h`; `tests/integration/elf_bad.sh`.
* `pemu_open_priv` (fake root's temporary chmod) pinned the file by its host NAME again after the race-free walk had already reached it; it now pins the same file through the same `RESOLVE_IN_ROOT` walk.
* Fixed (formerly "not changed, known"): (a) two `PT_LOAD` segments that share one page (hand-made or `ld -N` files) lost the tail of the first one, because the second segment's anonymous `mmap` replaced the shared page; both loaders now map only the pages after the previous segment, make the shared page writable for the `pread`, and then give it the later segment's protection, as the kernel does (`tests/integration/elf_bad.sh`, file `shared_page`: exit 90 on the old loader, 0 now). (b) A setuid / setgid file always gave id 0, even when its fake owner (see "Fake owners") was someone else; `pemu_exec_ids` now uses the recorded owner and keeps 0 for a file with no record or with `PEMU_FAKE_OWNER=0` (`t_owner_suid_lock` in `tests/unit/test_pemu_fixes.c`). (c) The read-modify-write of a partial `chown` (`-1` for one id) is serialised between threads by `pemu_ownlk` (released in a fork child by `pemu_atfork_child`); a plain read is one atomic `getxattr` and needs no lock. The same test chowns `1000:-1` and `-1:50` from two threads 20000 times: 2 lost updates before on a single core, 0 now. Still best effort: two separate pemu processes (after a fork) that make a partial `chown` of the same file at the same instant share only the xattr, so one half can still be lost there.

## Round 8: aarch64 libc-free build, long double (`host/rt/rt_quad.h`)

* `NOLIBC=1` is now accepted on aarch64 hosts (with a warning). Long double there is binary128; `rt_quad.h` implements sqrt, fma, fmod, remainder, ldexp, frexp, rint/floor/ceil/trunc/round, nextafter (exact), expm1 and atan2 (within 5 ulp measured) on integers.
* Checked on x86_64 (same code, `__float128`): `tests/integration/rt_quad.sh` = bit for bit against libquadmath (nearest) and an exact Fraction oracle for fma / sqrt / ldexp in all four rounding modes (0 mismatches). Cross-compiled with clang for aarch64: the only undefined symbols are libgcc's soft-float helpers.
* `sigset_t` words are no longer accessed through glibc's `__val` (`RT_SSW`), so musl / bionic do not break on that name.
* Not verified: any run on real aarch64 (FPCR / FPSR asm, `rt_core.h` aarch64 asm, the whole emulator on a quad `long double`), and musl / bionic builds (`__sigsetjmp`, `jmp_buf` layout, `ucontext` fields are still taken from the system headers).

### Round 8b: no libc headers (`host/abi/include/`)

* The NOLIBC build is compiled with `-nostdinc`: types, struct layouts (stat, sigaction, siginfo, ucontext, epoll_event, dirent, msghdr ...), flags, errno and syscall numbers (generated from the kernel's tables, x86_64 and aarch64) and the prototypes of everything `host/rt/` defines are in `host/abi/include/`. No glibc / musl / bionic header is read, so their differences (`__val` vs `__bits`, `jmp_buf` size, `ucontext` fields) no longer matter. `sigjmp_buf` is 200 bytes (the size the rt asm needs).
* Added to `host/rt/` because the aarch64 JIT uses them and the x86_64 build did not: `getauxval`, `flock`, `bcmp`.
* Checked: full `build.sh` suite with `NOLIBC=1 -nostdinc` on x86_64 (exit 0, `nm -u` empty); `tests/integration/nolibc_headers.sh`; `pemu_x64.c` compiles for `--target=aarch64-linux-gnu` with clang, undefined symbols only the compiler's (libgcc soft-float, `__clear_cache`, linker symbols).
* Not checked: linking or running on aarch64 (no aarch64 libgcc / qemu here); that the struct layouts equal the kernel's on aarch64 is by reading asm-generic/stat.h, signal and ucontext definitions, not by running; musl / bionic builds are no longer needed for the headers but the kernel of an Android device (seccomp) is as before untested.

### Round 8c: the libc build is gone

* `build.sh` has one mode: no libc (`-nostdlib -nostdinc -static-pie`, `-lgcc` for the compiler's soft-float). `NOLIBC=`, `STATIC=` and `PEMU_NOLIBC` are removed; `host/pemu_host.h` includes `host/rt/` unconditionally. The core check is built the same way. The five host-side unit-test programs (`tests/test_*.c`, `-DPEMU_UNIT_TEST_LIBC`) and the four scripts that link pemu.c into pemu_x64.c as one test program (`acq`, `jit_cache`, `jit_embed`, `jit_mprotect`: two runtimes would collide) are the only things that still use the system's libc: they drive linux-user/ with sockets, `system()`, pthread barriers and the like, which the product does not need and the runtime does not implement. Guest test fixtures compiled by tests/*.sh run inside the emulator and are not part of pemu.

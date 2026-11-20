# Tests

    tests/run.sh                # everything (./build.sh builds pemu, then calls this)
    tests/run.sh -l             # list
    tests/run.sh seccomp jit    # only tests whose name contains a word
    tests/run.sh -x             # stop at the first failure
    OUT=/path/to/build tests/run.sh      # a build dir other than ./build

| directory | what | run by `run.sh` |
|---|---|---|
| `unit/test_*.c` | host-native C programs that include `src/proot/` and drive it with the system libc (sockets, `system()`, threads) | yes, each compiled and run |
| `unit/check_core.c`, `unit/abi_layout.inc.h`, `unit/rt_quad_*` | sources used by `build.sh` and by integration tests (not tests by themselves) | no |
| `unit/rt_libc_test.c` | source of `integration/rt_libc.sh`: the same program built against `host/rt/` and against the system libc must print the same (printf floats, sscanf floats, FILE modes, qsort, posix_memalign, a malloc stress) | no |
| `integration/*.sh` | end to end (an x86_64 guest under `$OUT/pemu`) and static checks (no libc symbols, ABI layout ...). One argument: the build dir | yes, each |
| `guests/` | programs the tests run as guests (and generators of guests) | - |
| `data/` | expected outputs | - |
| `oracle/aarch64/` | an AArch64 interpreter, never linked into `pemu`: it runs the code the JIT emits on an x86_64 machine (see its README) | used by the `jit_*` and `acq` tests |
| `manual/` | not part of the suite: `jit_hw.sh` (on real aarch64 hardware), `clang_check.sh` (needs the real clang + lld), `rt_math.sh` (against glibc libm) | no |

## Adding a test

- **Unit test**: drop `tests/unit/test_<name>.c`. It is compiled with `-DPEMU_UNIT_TEST_LIBC -Isrc/proot` and must exit non-zero on failure. Put `run: quiet-stderr` in a comment in its first five lines if it prints expected errors.
- **Integration test**: drop `tests/integration/<name>.sh`, taking the build dir as `$1` (default `build`), `cd "$(dirname "$0")/../.."` first, exit non-zero on failure, and print `<name>: skipping ...` and exit 0 when a tool it needs is missing,
  unless `PEMU_REQUIRE_TESTS` is set (then fail: CI must not turn a skipped test into a green one). `e2e.sh` is a template.
- Put guests in `tests/guests/`, expected output in `tests/data/`. Nothing needs registering anywhere.

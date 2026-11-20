#!/bin/sh
# Runs the tests that need clang + lld (e2e, deny, plog) with PEMU_REQUIRE_TESTS=1, so none of them can "skip" and still look green.
# Meant for a machine with the real toolchain (the CI image, or `apt install clang lld`). A gcc wrapper named clang does not count: the tests
# call `clang --target=x86_64-linux-gnu ... -fuse-ld=lld`, and this script prints what it found so that is visible.
# usage: tests/manual/clang_check.sh [BUILD_DIR]      (build first: ./build.sh, or at least the pemu binary)
cd "$(dirname "$0")/../.."
B=${1:-build}
[ -x "$B/pemu" ] || { echo "clang_check: $B/pemu not found, run ./build.sh first" >&2; exit 2; }
for c in clang ld.lld; do
    command -v $c >/dev/null 2>&1 || { echo "clang_check: $c not found in PATH" >&2; exit 2; }
    p=$(command -v $c); echo "$c: $p ($($c --version 2>&1 | head -1))"
done
clang --version 2>&1 | grep -qi clang || { echo "clang_check: 'clang' does not identify itself as clang (a wrapper or shim?)" >&2; exit 2; }
ld.lld --version 2>&1 | grep -qi 'lld' || { echo "clang_check: 'ld.lld' does not identify itself as lld (a wrapper or shim?)" >&2; exit 2; }
export PEMU_REQUIRE_TESTS=1
rc=0
for t in e2e deny plog; do
    echo "== $t"
    sh tests/$t.sh "$B" || rc=1
done
[ $rc = 0 ] && echo "clang_check: e2e, deny, plog passed with clang $(clang --version | sed -n 's/.*version \([0-9.]*\).*/\1/p;q') and lld" || echo "clang_check: FAILED" >&2
exit $rc

#!/bin/sh
# host/rt/ (the libc-free runtime) against the system libc: tests/unit/rt_libc_test.c is built once with host/rt/ (-nostdlib -nostdinc, like pemu itself) and once with the
# system libc; both must print the same (printf %f %e %g, sscanf, FILE in r+ / w+ / a+, qsort, posix_memalign).  x86_64 host with gcc and a libc.
# usage: tests/integration/rt_libc.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = x86_64 ] || { echo "rt_libc: x86_64 host required, skipping"; exit 0; }
CC=${CC:-gcc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
$CC -O1 -w -o "$T/ref" tests/unit/rt_libc_test.c || { echo "rt_libc: cannot build the libc reference, skipping"; exit 0; }
$CC -O2 -w -DPEMU_RT_TEST -static-pie -fPIE -nostdlib -nostdinc -I"$(pwd)/host/abi/include" -I. -fno-math-errno -fno-stack-protector -o "$T/rt" tests/unit/rt_libc_test.c -lgcc \
  || { echo "rt_libc: FAILED, the test does not build against host/rt/"; exit 1; }
"$T/ref" "$T" > "$T/ref.out"
"$T/rt" "$T" > "$T/rt.out" || { echo "rt_libc: FAILED, the runtime build crashed"; exit 1; }
if cmp -s "$T/ref.out" "$T/rt.out"; then echo "rt_libc: $(wc -l < "$T/rt.out") lines identical to the system libc: ok"
else echo "rt_libc: FAILED, first differences (libc <, host/rt >):"; diff "$T/ref.out" "$T/rt.out" | head -30; exit 1; fi

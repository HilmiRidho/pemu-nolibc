#!/bin/sh
# host/abi/include against the system's own headers (glibc): every struct size / offset and flag value that pemu shares with the kernel must be identical.
# Both variants compile tests/unit/abi_layout.inc.h to a table of constants (gcc -S); the two tables are compared.  Skips when the system headers are missing.
CC=${CC:-gcc}; D=$(mktemp -d); trap 'rm -rf "$D"' EXIT; R=$(cd "$(dirname "$0")/../.." && pwd); T=$R/tests
cat > "$D/sys.c" <<EOC
#define _GNU_SOURCE
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <poll.h>
#include <dirent.h>
#include <signal.h>
#include <ucontext.h>
#include <setjmp.h>
#include <elf.h>
#include <fcntl.h>
#include <fenv.h>
#include <errno.h>
#include <time.h>
#include <sys/auxv.h>
#include <sched.h>
#include <linux/sched.h>
#include "$T/unit/abi_layout.inc.h"
EOC
printf '#include <pemu_abi.h>\n#include "%s/unit/abi_layout.inc.h"\n' "$T" > "$D/abi.c"
if ! $CC -w -S -o "$D/sys.s" "$D/sys.c" 2>"$D/err"; then echo "abi_layout: the system's headers do not compile here, skipping"; exit 0; fi
$CC -w -nostdinc -I"$R/host/abi/include" -S -o "$D/abi.s" "$D/abi.c" || exit 1
vals() { grep -E '^\s+\.(quad|long)\s' "$1" | awk '{print $2}'; }
vals "$D/sys.s" > "$D/a.txt"; vals "$D/abi.s" > "$D/b.txt"
if cmp -s "$D/a.txt" "$D/b.txt"; then echo "abi_layout: $(wc -l < "$D/a.txt") sizes, offsets and flags equal the system headers': ok"; exit 0; fi
echo "abi_layout: MISMATCH (line numbers count the entries of tests/unit/abi_layout.inc.h)"; diff "$D/a.txt" "$D/b.txt" | head -20; exit 1

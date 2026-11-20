#!/bin/sh
# The build reads no header of the system: compile with -nostdinc and only host/abi/include on the path.
#  - the host's own compiler: pemu compiles to an object, `nm -u` shows nothing but the compiler's own symbols (libgcc soft-float, __clear_cache, linker symbols)
#  - clang for aarch64 (when present): src/pemu.c compiles for an aarch64 host too, without any aarch64 sysroot
CC=${CC:-gcc}; D=$(mktemp -d); trap 'rm -rf "$D"' EXIT; R=$(cd "$(dirname "$0")/../.." && pwd); I="-nostdinc -I$R/host/abi/include -DPEMU_RELEASE=\"t\" -fno-stack-protector -Werror=implicit-function-declaration -w"
OK='^(__(add|sub|mul|div|udiv|mod|umod|eq|ne|lt|le|gt|ge|unord|ext|trunc|fix|float|neg|cmp|popcount[a-z]*|clear_cache|ehdr_start|executable_start|init_array|fini_array|preinit_array|bswap)|_?etext|bcmp|memcpy|memset|memmove|memcmp|_GLOBAL_OFFSET_TABLE_|_DYNAMIC)'
check() {  # $1 compiler + flags, $2 source, $3 label
  $1 -c -O2 -fPIE $I "$R/$2" -o "$D/o.o" || { echo "nolibc_headers: $3: compile failed"; exit 1; }
  bad=$(nm -u "$D/o.o" 2>/dev/null || llvm-nm -u "$D/o.o") ; bad=$(echo "$bad" | awk '{print $NF}' | grep -Ev "$OK" | grep -v '^$')
  [ -z "$bad" ] || { echo "nolibc_headers: $3: undefined symbols: $bad"; exit 1; }
  echo "nolibc_headers: $3: ok (no system header, no libc symbol)"
}
check "$CC" src/pemu.c "src/pemu.c $(uname -m) host"
if command -v clang >/dev/null 2>&1 && clang --print-targets 2>/dev/null | grep -q aarch64; then
  check "clang --target=aarch64-linux-gnu" src/pemu.c "src/pemu.c aarch64 host (cross, compile only)"
fi

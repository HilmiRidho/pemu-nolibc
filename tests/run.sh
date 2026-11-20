#!/bin/sh
# Runs the test suite: host-native C unit tests (tests/unit/test_*.c) and the end-to-end / static checks (tests/integration/*.sh).
# Nothing is listed here: a test is a file in one of those two places (see tests/README.md), so adding one needs no edit to build.sh.
#
#   tests/run.sh                  run everything (needs a built $OUT/pemu: ./build.sh builds it, then calls this script)
#   tests/run.sh -l               list the tests
#   tests/run.sh jit seccomp      only the tests whose name contains one of the words (names look like unit/test_deny, integration/jit_cache)
#   tests/run.sh -x               stop at the first failure (default: run all, summarise, exit 1 if any failed)
#
# Environment: OUT (build dir, default ./build), CC (default gcc), PEMU_REQUIRE_TESTS=1 (CI: a test that cannot run because a tool is missing FAILS instead of skipping).
# A unit test whose first lines contain "run: quiet-stderr" has its stderr dropped (it prints expected errors on purpose).
cd "$(dirname "$0")/.." || exit 2
CC=${CC:-gcc}
OUT=${OUT:-build}; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
LIST= FAILFAST= PATS=
for a in "$@"; do
  case "$a" in -l|--list) LIST=1 ;; -x|--fail-fast) FAILFAST=1 ;; -h|--help) sed -n '2,12p' "$0"; exit 0 ;; -*) echo "run.sh: unknown option $a" >&2; exit 2 ;; *) PATS="$PATS $a" ;; esac
done
names() { for f in tests/unit/test_*.c; do b=${f##*/}; echo "unit/${b%.c}"; done; for f in tests/integration/*.sh; do b=${f##*/}; echo "integration/${b%.sh}"; done; }
wanted() { [ -z "$PATS" ] && return 0; for p in $PATS; do case "$1" in *"$p"*) return 0 ;; esac; done; return 1; }
if [ -n "$LIST" ]; then names | while read -r n; do wanted "$n" && echo "$n"; done; exit 0; fi

# the host-side unit tests are the one place the system's libc is used: they exercise src/proot/ in isolation (sockets, system(), pthread barriers ...).
# bionic (Android) has pthread inside libc and recent NDKs ship no libpthread to link against
case "$($CC -dumpmachine 2>/dev/null)" in *android*) TPTH="" ;; *) TPTH="-lpthread" ;; esac
# every run writes ~/.pemu/pemu.log (src/proot/pemulog.inc.h): keep the tests out of the real one
TESTHOME=$(mktemp -d); trap 'rm -rf "$TESTHOME"' EXIT; export HOME=$TESTHOME PEMU_REQUIRE_TESTS
LOG=$TESTHOME/.run.log; RC=$TESTHOME/.run.rc
pass=0 fail=0 skip=0 failed=
run_one() {   # $1 name, rest: the command
  n=$1; shift
  echo "== $n"
  start=$(date +%s)
  { "$@" 2>&1; echo $? > "$RC"; } | tee "$LOG"
  rc=$(cat "$RC"); s=$(( $(date +%s) - start ))
  if [ "$rc" != 0 ]; then fail=$((fail+1)); failed="$failed $n"; echo "-- FAILED: $n (exit $rc, ${s}s)"
  elif grep -q 'skipping' "$LOG"; then skip=$((skip+1)); echo "-- ok, with skipped parts: $n (${s}s)"
  else pass=$((pass+1)); fi
  [ -n "$FAILFAST" ] && [ "$rc" != 0 ] && summary
}
summary() {
  echo; echo "tests: $pass passed, $skip passed with skips, $fail failed"
  [ -z "$failed" ] || echo "failed:$failed"
  [ "$fail" = 0 ]; exit $?
}
unit() {      # compile tests/unit/<n>.c against src/proot with the system's libc and run it
  f=tests/unit/$1.c
  $CC -O0 -DPEMU_UNIT_TEST_LIBC -Wno-unused-function -Isrc/proot -o "$OUT/$1" "$f" $TPTH || return 1
  if head -5 "$f" | grep -q 'run: quiet-stderr'; then "$OUT/$1" 2>/dev/null; else "$OUT/$1"; fi
}
names | while read -r n; do wanted "$n" && echo "$n"; done > "$TESTHOME/.sel"
while read -r n; do
  case "$n" in
    unit/*) run_one "$n" unit "${n#unit/}" ;;
    integration/*) run_one "$n" sh "tests/$n.sh" "$OUT" ;;
  esac
done < "$TESTHOME/.sel"
summary

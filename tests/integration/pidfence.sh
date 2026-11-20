#!/bin/sh
# PID fence (src/proot/pidfence.inc.h): a guest may only name processes of its own tree.  An x86_64 guest under pemu in a rootfs
# is given the PID of a HOST process it did not start (a "victim" that records SIGUSR1/SIGUSR2 in a file) and must get ESRCH for kill,
# tgkill, pidfd_open, sched_getaffinity, getpgid, getsid, getpriority, ioprio_get and capget; its own children must keep working; kill(0,s)
# and kill(-1,s) must reach them and not the victim; setpriority(PRIO_USER) must not nice the victim.  PEMU_PID_FENCE=0 turns the fence
# off (probe only: kill(victim,0) then succeeds, which shows the test can tell the difference).  Everything runs in its own session
# (setsid), so a broken fence cannot signal this script.
# Skips (exit 0) when the host is not x86_64, gcc cannot link statically or setsid is missing.  usage: tests/integration/pidfence.sh [BUILD_DIR]
cd "$(dirname "$0")/../.."
B=${1:-build}; B=$(cd "$B" && pwd)
[ "$(uname -m)" = x86_64 ] || { echo "pidfence: x86_64 host required, skipping"; exit 0; }
command -v setsid >/dev/null 2>&1 || { echo "pidfence: setsid missing, skipping"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -O1 -static -o "$T/g" tests/guests/pidfence_guest.c 2>/dev/null || { echo "pidfence: cannot build a static guest, skipping"; exit 0; }
mkdir -p "$T/rf"; cp "$T/g" "$T/rf/g"
cat > "$T/run.sh" <<EOS
sh -c 'trap "echo got > $T/got" USR1 USR2; while :; do sleep 0.2; done' &
V=\$!
sleep 0.5
N0=\$(awk '{print \$19}' /proc/\$V/stat)
timeout 120 "$B/pemu" -r "$T/rf" --no-default-binds /g \$V > "$T/out" 2>&1
PEMU_PID_FENCE=0 timeout 60 "$B/pemu" -r "$T/rf" --no-default-binds /g \$V probe > "$T/off" 2>&1
N1=\$(awk '{print \$19}' /proc/\$V/stat 2>/dev/null)
kill -0 \$V 2>/dev/null && echo alive > "$T/alive"
echo "\$N0 \$N1" > "$T/nice"
kill -9 \$V 2>/dev/null
EOS
setsid -w sh "$T/run.sh" >/dev/null 2>&1
rc=0; out=$(cat "$T/out")
chk() { echo "$out" | grep -q "^$1\$" || { echo "pidfence: expected '$1' in:"; echo "$out"; rc=1; }; }
for w in "kill(victim,0)" "tgkill(victim)" "pidfd_open(victim)" "sched_getaffinity(victim)" "getpgid(victim)" "getsid(victim)" \
         "getpriority(victim)" "ioprio_get(victim)" "capget(victim)"; do chk "$w: No such process"; done
chk "ioprio_set(user): Operation not permitted"
for w in "kill(self,0)" "kill(child,0)" "tgkill(self)" "sched_getaffinity(child)" "getpgid(child)" "kill(-1,0)" "kill(0,USR1)" "kill(-1,USR2)" \
         "setpriority(user)" "getpriority(user)" "mq_open" "mq_unlink"; do chk "$w: ok"; done
chk "child got USR1: yes"; chk "self got USR1: yes"; chk "child got USR2: yes"
chk "kill(-1,0) alone: No such process"
chk "mq_unlink again: No such file or directory"
chk "reexec kill(victim,0): No such process"; chk "reexec getpgid(victim): No such process"   # the fence survives an execve, and a guest cannot disable it through its environment
[ -e "$T/got" ] && { echo "pidfence: the victim received a signal sent by the guest"; rc=1; }
[ -e "$T/alive" ] || { echo "pidfence: the victim was killed"; rc=1; }
[ "$(cut -d' ' -f1 "$T/nice")" = "$(cut -d' ' -f2 "$T/nice")" ] || { echo "pidfence: the victim's nice value changed: $(cat "$T/nice")"; rc=1; }
grep -q "^probe kill(victim,0): ok$" "$T/off" || { echo "pidfence: PEMU_PID_FENCE=0 must leave kill alone: $(cat "$T/off")"; rc=1; }
[ $rc = 0 ] && echo "pidfence pemu: ok" || echo "pidfence pemu: FAILED"
exit $rc

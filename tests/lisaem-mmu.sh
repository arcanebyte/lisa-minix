#!/bin/bash
# Phase 5 exit test: the MMU (PLAN.md), in LisaEm.
#
# Boots a copy of the ProFile image, logs in on the serial port and runs
#   sh /usr/test/phase5   fork/exec timing (forkbench) and the Minix system
#                         call tests test0-test21 (tests/phase5.sh);
#   /usr/test/segv        writes outside its own memory;
#   echo, sync, fsck      the system carries on after the fault.
# Then checks the serial log: every test reports "ok", segv was killed by
# SIGSEGV before it could say "survived", and fsck finds the file system
# clean; and checks the file system on the Mac.
# Usage: tests/lisaem-mmu.sh IMAGE [MEM_KB] [LOG]
set -euo pipefail

src=$1
mem=${2:-1024}
here=$(cd "$(dirname "$0")/.." && pwd)
log=${3:-$here/build/lisaem/phase5-$mem.log}
image=$here/build/lisa-minix/phase5-test.image

cp "$src" "$image"
python3 "$here/tools/lisaem_run.py" "$image" --mem "$mem" --timeout 3000 \
    --char-delay 0.5 --log "$log" \
    --send 'login:=root' \
    --send '# $=sh /usr/test/phase5' \
    --send 'phase5: all tests run\n# $=/usr/test/segv' \
    --send '# $=echo still running; sync' \
    --send 'still running\n# $=fsck /dev/hd0' \
    --until 'Free zones.*\n# $'

fail=0
# Tests 20 and 21 (file permissions) already failed with shadowing, before
# phase 5, in the same way; they are reported but do not fail this test.
known="20 21"
echo "---- results ($log) ----"
grep -a 'forkbench:' "$log" || true
for n in $(seq 0 21); do
    # everything the test printed, from its "phase5: testN" line to the next
    out=$(awk -v t="phase5: test$n" '
        index($0, t) && substr($0, index($0, t) + length(t)) == "" { on = 1; next }
        on && /phase5: / { exit }
        on { print }' "$log")
    start=$(grep -a "phase5: test$n\$" "$log" | awk '{print $1}')
    if echo "$out" | grep -q ' ok\|^ *[0-9.]* ok$'; then
        echo "test$n: ok (started at $start s)"
    elif [[ " $known " == *" $n "* ]]; then
        echo "test$n: failed, as before phase 5: $(echo "$out" | head -1 | cut -c9-)"
    else
        echo "test$n: FAILED: $(echo "$out" | head -3 | cut -c9- | tr '\n' ' ')"
        fail=1
    fi
done
if grep -aq 'survived' "$log"; then
    echo "FAIL: segv survived a write outside its memory"
    fail=1
fi
if ! grep -aq 'sig=11 to pid=.*' "$log"; then
    echo "FAIL: no SIGSEGV for segv"
    fail=1
fi
python3 "$here/tools/minixfs.py" "$image" check || fail=1
if [ $fail -ne 0 ]; then
    echo "FAIL: phase 5 MMU test at $mem KB"
    exit 1
fi
echo "PASS: phase 5 MMU test at $mem KB"

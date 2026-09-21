#!/bin/bash
# Test of the multi-file commands (src/commands/<dir>), in LisaEm.
#
# Types on the Lisa keyboard: runs /usr/test/cmds (tests/cmds.sh: make, m4,
# bawk, nroff, patch, ar, indent without a terminal), then edits a file
# with elvis (insert, Escape, :wq) and one with mined (type, ^W, ^X), asks
# kermit for its version, and syncs. Checks the command output in the serial log and the two edited
# files on the Mac with tools/minixfs.py.
# Usage: tests/lisaem-cmds.sh IMAGE [MEM_KB]
set -euo pipefail

src=$1
mem=${2:-1024}
here=$(cd "$(dirname "$0")/.." && pwd)
image=$here/build/lisa-minix/cmds-test.image
log=$here/build/lisaem/cmds-$mem.log

cp "$src" "$image"
esc=$(printf '\033')
ctlw=$(printf '\027')
ctlx=$(printf '\030')
python3 "$here/tools/lisaem_run.py" "$image" --keyboard --mem "$mem" \
    --timeout 600 --send-wait 6 --log "$log" \
    --send 'login:=root' \
    --send '# $=sh /usr/test/cmds' \
    --send "cmds: done\\n# \$=elvis /tmp/e.txt" \
    --send "=itext typed in elvis${esc}:wq" \
    --send '# $=cat /tmp/e.txt' \
    --send '# $=mined /tmp/m.txt' \
    --send '=text typed in mined' \
    --send "=${ctlw}${ctlx}" \
    --send '# $=kermit' \
    --send '=version' \
    --send '=exit' \
    --send '# $=cat /tmp/m.txt; sync; echo cmds; echo test' \
    --until 'cmds\ntest'

fail=0
check() {    # check NAME REGEX: the output after "cmds: NAME" matches
    local out
    out=$(awk -v t="cmds: $1" 'index($0, t) { on = 1; next } on && /cmds: / { exit } on { print }' "$log")
    if echo "$out" | grep -Eq "$2"; then
        echo "ok: $1"
    else
        echo "FAIL: $1: $(echo "$out" | head -3 | tr '\n' ' ')"
        fail=1
    fi
}
check make '^ *[0-9.]+ hi$'
check m4 'hello Lisa'
check bawk 'two'
check nroff 'centered text'
check patch 'line two'
check ar '^ *[0-9.]+ b$'
check indent 'x = 1;'
if grep -aq 'C-Kermit' "$log"; then echo "ok: kermit"; else echo "FAIL: kermit: no version"; fail=1; fi
for f in e:elvis m:mined; do
    got=$(python3 "$here/tools/minixfs.py" "$image" get /tmp/${f%%:*}.txt || true)
    if [ "$got" = "text typed in ${f##*:}" ]; then
        echo "ok: ${f##*:} wrote /tmp/${f%%:*}.txt"
    else
        echo "FAIL: ${f##*:}: /tmp/${f%%:*}.txt is [$got]"
        fail=1
    fi
done
python3 "$here/tools/minixfs.py" "$image" check || fail=1
[ $fail -eq 0 ] && echo "PASS: multi-file commands at $mem KB" || { echo "FAIL: multi-file commands"; exit 1; }

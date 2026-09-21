#!/bin/bash
# Phase 4 exit test: a shell on the Lisa's own screen and keyboard, in LisaEm.
#
# Boots a copy of the ProFile image and does everything through the Lisa
# keyboard (tools/lisaem_run.py --keyboard). Ten seconds in, LisaEm moves the
# mouse once (LISAEM_MOUSE_MOVE_AT), which used to hang the keyboard driver
# (docs/lisaem.md). Then it logs in, lists /bin (scrolls the
# screen), writes every shifted symbol on the US keyboard to a file with
# echo, and writes a second file with cat ended by Command-D (control-D).
# Then checks, on the Mac:
#   - both files hold exactly what was typed (tools/minixfs.py);
#   - holding the A key down for a few seconds (raw COPS codes through
#     LisaEm) repeats it: `echo` prints at least 20 a's;
#   - `date`, typed after login, shows the time LisaEm's clock gave the COPS
#     (the Mac's local time of day), within ten minutes of the end of the run;
#   - the text on the Lisa screen, read from LisaEm's screen dump with
#     tools/lisascreen.py, shows the last command and the prompt.
# Usage: tests/lisaem-console.sh IMAGE [MEM_KB]
set -euo pipefail

src=$1
mem=${2:-1024}
here=$(cd "$(dirname "$0")/.." && pwd)
image=$here/build/lisa-minix/phase4-test.image
log=$here/build/lisaem/phase4-$mem.log
screen=$here/build/lisaem/screen.png

cp "$src" "$image"
# Not in the list: '@', Minix's line-kill character in cooked mode, and '|',
# which LisaEm types as Shift and the '/?' key (docs/lisaem.md).
symbols='~!#$%^&*()_+{}:"<>? `-=[]\;,./ AZaz09'
eof=$(printf '\004')

python3 "$here/tools/lisaem_run.py" "$image" --keyboard --mem "$mem" \
    --timeout 400 --linger 3 --log "$log" --env LISAEM_MOUSE_MOVE_AT=10 \
    --send 'login:=root' \
    --send '# $=date' \
    --send '# $=echo \c' \
    --send "echo \$=$(printf '\001\360')\\c" \
    --send "aaaaaaaaaa=$(printf '\001\160')" \
    --send '# $=ls /bin' \
    --send "# \$=echo '$symbols' > /tmp/keys" \
    --send '# $=cat > /tmp/typed' \
    --send '=typed on the Lisa keyboard' \
    --send "=$eof\\c" \
    --send '# $=sync; cat /tmp/typed' \
    --until 'typed on the Lisa keyboard\n# $'

# Auto-repeat: the echo of the held key.
if ! grep -aEq '^ *[0-9.]+ a{20,}$' "$log"; then
    echo "FAIL: holding a key did not repeat it"
    exit 1
fi
echo "auto-repeat: $(grep -aEo 'a{20,}' "$log" | tail -1 | tr -d '\n' | wc -c) a's"

# Time of day from the Lisa clock (the year is the COPS's 1980-1995 one).
python3 - "$log" <<'PY'
import re, sys, time
text = open(sys.argv[1], errors="replace").read()
m = re.search(r"# date\n\s*[\d.]+ \w{3} \w{3} +\d+ (\d\d):(\d\d):(\d\d) (\d{4})", text)
if not m:
    sys.exit("FAIL: no date output")
lisa = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
now = time.localtime()
mac = now.tm_hour * 3600 + now.tm_min * 60 + now.tm_sec
diff = min(abs(mac - lisa), 86400 - abs(mac - lisa))
print("Lisa clock: %s:%s:%s %s, Mac %02d:%02d:%02d, run started up to a few minutes ago"
      % (m.group(1), m.group(2), m.group(3), m.group(4), now.tm_hour, now.tm_min, now.tm_sec))
if diff > 600:
    sys.exit("FAIL: Lisa time of day is %d s from the Mac's" % diff)
PY

got=$(python3 "$here/tools/minixfs.py" "$image" get /tmp/keys)
if [ "$got" != "$symbols" ]; then
    echo "FAIL: /tmp/keys is [$got], expected [$symbols]"
    exit 1
fi
got=$(python3 "$here/tools/minixfs.py" "$image" get /tmp/typed)
if [ "$got" != "typed on the Lisa keyboard" ]; then
    echo "FAIL: /tmp/typed is [$got]"
    exit 1
fi

text=$(python3 "$here/tools/lisascreen.py" "$screen")
echo "---- Lisa screen ----"
echo "$text"
echo "---------------------"
last=$(echo "$text" | grep -v '^$' | tail -3)
expected=$'# sync; cat /tmp/typed\ntyped on the Lisa keyboard\n#'
if [ "$last" != "$expected" ]; then
    echo "FAIL: screen ends with [$last]"
    exit 1
fi
python3 "$here/tools/minixfs.py" "$image" check
echo "PASS: phase 4 console test at $mem KB"

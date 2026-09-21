#!/bin/bash
# Test of the Sony floppy driver (src/kernel/lisa/lisafloppy.c), in LisaEm.
#
# Makes a 400K DC42 disk with a Minix file system holding one file
# (tools/mkfs.py, tools/mkdc42.py), boots the ProFile image, has LisaEm
# insert the disk ten seconds in (LISAEM_FLOPPY_AT), and types on the Lisa
# keyboard: mount /dev/fd0, cat the file, copy /etc/motd and two large
# programs (kermit and elvis, about 170 KB, so the disk's first speed zones
# are used) onto the disk, umount. Then checks, on the Mac, that the disk
# has the files with the right contents and a clean file system.
# Usage: tests/lisaem-floppy.sh IMAGE [MEM_KB]
set -euo pipefail

src=$1
mem=${2:-1024}
here=$(cd "$(dirname "$0")/.." && pwd)
work=$here/build/floppy
mkdir -p "$work"
image=$work/floppy-test.image
disk=$work/floppy-test.dc42
log=$here/build/lisaem/floppy-$mem.log

cd "$here"
python3 tools/mkfs.py "$work/floppy.fs" tests/floppy/floppy.proto
python3 tools/mkdc42.py create "$disk" "$work/floppy.fs"
cp "$src" "$image"
python3 tools/lisaem_run.py "$image" --keyboard --mem "$mem" --timeout 300 \
    --log "$log" --env LISAEM_FLOPPY_AT=10,"$disk" \
    --send 'login:=root' \
    --send '# $=mount /dev/fd0 /mnt' \
    --send '# $=cat /mnt/hello' \
    --send '# $=cp /etc/motd /mnt/motd' \
    --send '# $=cp /bin/kermit /bin/elvis /mnt' \
    --send '# $=umount /dev/fd0' \
    --send '# $=echo floppy; echo test' \
    --until 'floppy\ntest'

fail=0
grep -aq '/dev/fd0 mounted' "$log" || { echo "FAIL: mount"; fail=1; }
grep -aq 'written on the Mac' "$log" || { echo "FAIL: reading the file"; fail=1; }
grep -aq '/dev/fd0 unmounted' "$log" || { echo "FAIL: umount"; fail=1; }
# LisaEm does not update the DC42 checksums when the guest writes.
python3 tools/mkdc42.py data "$disk" "$work/after.fs"
if [ "$(python3 tools/minixfs.py "$work/after.fs" get /hello)" != "written on the Mac" ]; then
    echo "FAIL: /hello changed"; fail=1
fi
if ! python3 tools/minixfs.py "$work/after.fs" get /motd | cmp -s - rootfs/etc/motd; then
    echo "FAIL: /motd on the floppy differs from rootfs/etc/motd"; fail=1
fi
for prog in kermit elvis; do
    if ! python3 tools/minixfs.py "$work/after.fs" get /$prog | cmp -s - build/lisa-minix/cmd/$prog; then
        echo "FAIL: /$prog on the floppy differs from build/lisa-minix/cmd/$prog"; fail=1
    fi
done
python3 tools/minixfs.py "$work/after.fs" check || fail=1
[ $fail -eq 0 ] && echo "PASS: floppy at $mem KB" || { echo "FAIL: floppy"; exit 1; }

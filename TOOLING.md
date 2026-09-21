# Tools

The Python programs in `tools/` build the images, move files in and out
of them on the Mac, and drive the emulators for the tests. They need only
Python 3 and its standard library.

Each tool describes itself in full in the comment at the top of the file.
Run it with no arguments to print that description, except `mklisa.py`
and `lisaem_run.py`, which take `-h`. This page is the overview: what each
tool is for and the commands used most often.

| Tool | What it does | Run by |
|---|---|---|
| `elf2mnx.py` | m68k ELF executable to Minix executable | `make` |
| `build.py` | kernel, mm, fs and init into one boot image | `make` |
| `mkfs.py` | Minix file system image from a prototype file | `make`, you |
| `mklisa.py` | boot block, boot image and file system into a ProFile image | `make` |
| `minixfs.py` | list, copy out and check files in an image | you, tests |
| `mkdc42.py` | Lisa 400K floppy images (DiskCopy 4.2) | you, tests |
| `lisascreen.py` | text on the Lisa screen, from a LisaEm screen dump | you, tests |
| `lisaem_run.py` | boot an image in LisaEm and script it | `make lisaem-*`, tests |
| `hatari_run.py` | boot the Atari ST build in Hatari and script it | `make hatari-*` |
| `ack2gas.py` | ACK assembly to GNU assembly (already applied) | nobody now |

## How `make` uses them

```
.c, .s ──m68k-minix-gcc, m68k-elf-ld──▶ .elf ──elf2mnx.py──▶ .mix
kernel.mix mm.mix fs.mix init.mix [root.fs] ──build.py --lisa──▶ minix.bin
rootfs/*.proto + commands ──mkfs.py──▶ root.fs / usr.fs
lisaboot.bin + minix.bin [+ usr.fs] ──mklisa.py──▶ minix.image / minix-hd.image
```

- **5 MB image** (`make lisa-minix`): the root file system is built into
  the boot image as a RAM disk (`build.py --lisa ... ROOTFS`).
- **10 MB image** (`make lisa-minix-hd`): `build.py` gets `-` for ROOTFS,
  and `mklisa.py --fs` places the file system on the ProFile after the
  boot image, where the kernel mounts it as `/dev/hd0`.

The files installed on each image are listed in `rootfs/lisa.proto` and
`rootfs/lisa-hd.proto` (the Makefile adds every command and test program
to the 10 MB one). To add a file, add a line to the prototype file and
rebuild.

## Looking inside an image: `minixfs.py`

Read-only; it never changes the image. It takes a ProFile image (from
`mklisa.py`, or a copy taken off an ESProFile SD card) or a plain file
system image (from `mkfs.py` or `mkdc42.py data`).

```
python3 tools/minixfs.py build/lisa-minix/minix-hd.image ls /bin
python3 tools/minixfs.py build/lisa-minix/minix-hd.image get /etc/passwd
python3 tools/minixfs.py profile.image get /tmp/notes.txt notes.txt
python3 tools/minixfs.py profile.image check
```

`check` does what `fsck -n` would and exits with 1 if the file system is
not clean. It is the quickest way to see whether a Lisa left the file
system intact, for example after a power-off without `sync`.

## Making a file system: `mkfs.py`

A port of Minix's own `mkfs`, taking the same prototype files (format in
the tool's description and in `mkfs(8)`). A 400K floppy with one file:

```
boot
400 64
d--755 2 2
hello ---644 2 2 tests/floppy/hello.txt
$
```

Line 2 is the size in 1 KB blocks and the number of inodes; line 3 is the
root directory's mode, owner and group; each file line is its name on the
disk, mode, owner, group and the file on the Mac, relative to where the
tool runs; `$` ends a directory.

```
python3 tools/mkfs.py build/floppy.fs tests/floppy/floppy.proto
```

## Floppies: `mkdc42.py`

LisaEm and floppy emulators read Sony 400K disks as DiskCopy 4.2 images.
To put files on a floppy for the Lisa, make a 400-block file system and
wrap it:

```
python3 tools/mkfs.py build/floppy.fs tests/floppy/floppy.proto
python3 tools/mkdc42.py create build/floppy.dc42 build/floppy.fs
```

On the Lisa: `mount /dev/fd0 /mnt`. To read a floppy written on the Lisa:

```
python3 tools/mkdc42.py data build/floppy.dc42 build/floppy-out.fs
python3 tools/minixfs.py build/floppy-out.fs ls /
python3 tools/mkdc42.py check build/floppy.dc42
```

LisaEm does not update the image's checksums when the Lisa writes to it,
so `check` can fail on a disk the emulator has written; `data` still
works.

`create` with no DATA makes a blank disk, for `mkfs /dev/fd0 400` on the
Lisa.

## The ProFile image: `mklisa.py`

`make` calls it; you would call it directly only to boot something other
than Minix through the same boot block, as the phase 1 test kernel does
(`make lisa`):

```
python3 tools/mklisa.py -f --entry ENTRY BOOTBLOCK PROGRAM.bin OUTPUT.image
```

PROGRAM.bin is a raw binary (`m68k-elf-objcopy -O binary`) linked to run
at `--load` (default 0), and ENTRY is the address of its start symbol.

`--size 10` makes a 10 MB image and `--fs FILE` adds a file system after
the boot image. The layout (block 0 boot block, block 1 header, then the
image and the file system, in 532-byte blocks) is in the tool's
description and `docs/disk-layout.md`. The output is always one of the
two sizes ESProFile accepts.

## Running in LisaEm: `lisaem_run.py`

Boots an image in LisaEm with nobody at the window, reads serial port B,
and types commands when the output matches. Needs LisaEm built from
upstream `master` at `3020b09` or later and a boot ROM H image
(`docs/lisaem.md`). It runs a copy of LisaEm under another name with its
own configuration, and checks that your own LisaEm preferences are
unchanged afterwards.

Minix writes to the disk it runs from, so run it on a copy:

```
cp build/lisa-minix/minix-hd.image build/try.image
python3 tools/lisaem_run.py build/try.image --timeout 300 \
    --send 'login:=root' --send '# $=ls -l /' --until 'usr\n# $'
```

- `--send RE=TEXT`: when the output matches RE, type TEXT and Return;
  TEXT ending in `\c` is typed without Return. Steps run in order.
- `--until RE`: stop with exit status 0 when the output matches;
  `--timeout` stops with 1.
- `--keyboard`: type on the Lisa keyboard instead of serial port B.
- `--mem 2048`: 2 MB (LisaEm built with `--allow2mbram`).
- `--rom`, `--lisaem`: another boot ROM or LisaEm.app.
- `--env NAME=VALUE`: extra LisaEm environment variables, such as
  `LISAEM_CPU_TRACE` (LisaEm's `EnvironmentVariables.md`).

Output: the serial log with times in `build/lisaem/serial.log` (or
`--log`), the screen in `build/lisaem/screen.png`, LisaEm's own messages
in `build/lisaem/lisaem.log`. `tools/lisaem-pram.txt` is the parameter RAM
it gives the Lisa, so that it starts up from the ProFile.

The `make lisaem-*` targets and `tests/lisaem-*.sh` are all built on it;
they are the best examples.

## Reading the screen: `lisascreen.py`

Turns a LisaEm screen dump back into text, by matching each character
cell against the Minix console font:

```
python3 tools/lisascreen.py build/lisaem/screen.png
```

It only reads what the Minix console draws (90 x 40 characters); other
screens come out blank, or as `?` with `--raw`.

## Atari ST reference build: `hatari_run.py`

Boots `build/atari/minix_fd.st` in Hatari without a window, with steps
such as `wait:15`, `key:28`, `disk:FILE` and `shot:NAME` (screenshot). Used
by `make hatari-boot` and `make hatari-shell`; setup in `docs/hatari.md`.

## One-off: `ack2gas.py`

Converted the original ACK assembler sources to GNU syntax when the tree
was first built with gcc. Every `.s` file has been converted and
reviewed; it is kept to show how, and would be needed only for more
Minix-ST assembly brought in from elsewhere. Details in
`docs/toolchain.md`.

# Hatari

Hatari is the reference system for the unmodified Atari build (plan
phase 0): if something fails there, it is a Minix or toolchain problem,
not a Lisa one.

## Setup

| Item | Version | Source |
|---|---|---|
| Hatari | 2.6.1 | `brew install hatari` |
| EmuTOS | 1.4, 512k US ROM `etos512us.img` | https://sourceforge.net/projects/emutos/files/emutos/1.4/ `emutos-512k-1.4.zip`, SHA-256 `1ef7bd25f61bcfc66d19debc2f0ebb0d5e5ba811ccd7f0fddfcb105bb72d1663`, unpacked in `~/opt/emutos` |

## Running

```
make hatari-boot
```

boots `build/atari/minix_fd.st` and saves `build/hatari/boot.png`.
`tools/hatari_run.py` does the work: it starts Hatari with SDL's dummy
video and audio drivers (no window), an ST with a monochrome monitor,
1 MB of RAM and fast floppy access, and drives it through
`--control-socket`: waits, screenshots (`hatari-shortcut screenshot`),
key presses (`hatari-event keypress`) and floppy swaps
(`hatari-option --disk-a`). Hatari's own log goes to `build/hatari/hatari.log`.
`--window` shows the emulator instead.

The `Bus Error reading at address ...` warnings with PC in `$e00xxx` at
start-up are EmuTOS probing for memory and hardware, before Minix is
loaded.

`make hatari-shell` is the phase 0 exit test: it boots `minix_fd.st`,
swaps in `root_fd.st` at the root diskette prompt, presses RETURN, logs
in as `root`, runs `ls -l /bin` and `cat /etc/passwd`, and saves
`build/hatari/shell.png`. It runs on a 1 MB ST, where Minix reports
`Memory size = 992K  MINIX = 146K  RAM disk = 360K  Available = 486K`.

Floppies are swapped with `hatari-option --disk-a FILE`; Hatari's
`hatari-path` has no floppy type.

## Root floppy

`rootfs/atari.proto` lists the root file system: `sh`, `login`, `ls`,
`cat`, `echo`, `pwd`, `mkdir`, `rm`, `cp` and `sync` in `/bin`; device
nodes in `/dev` (majors from `src/fs/table.c`: 1 memory, 2 floppy,
4 console, 5 tty, 6 printer); `/etc/passwd`, `group`, `ttys`, `rc`
and `motd` from `rootfs/etc`; `/tmp` and `/usr/adm`. `tools/mkfs.py`
builds the 360-block file system, and the Makefile writes the `type_fd`
BPB (`src/tools/type.s`) into the unused start of block 0 and pads it to
a 720-sector floppy.

`/etc/ttys` is `100`: login on the console, no serial lines. Root has no
password.

## Results

14 September 2026, gcc build of the Atari tree:

- `8ecbdd4`: TOS boots the floppy, the boot block loads the image, and
  Minix prints its banner and `Insert ROOT diskette and hit RETURN`, so
  kernel, mm and fs start, exchange messages, and the console works.
- With the root floppy, loading the RAM disk failed with
  `fd0: read: dma status = 0x1` and a "Root file system corrupted"
  panic. Cause: gcc dropped repeated writes to the DMA mode register
  (see `docs/toolchain.md`, "Hardware registers"). Fixed in `4f2e334`.
- After the fix: the RAM disk loads, `/etc/rc` runs
  (`Minix root file system loaded.`), `login` accepts `root`, the motd
  prints, and the shell runs `ls -l /bin` and `cat /etc/passwd`.
  **Phase 0 exit test passed.**

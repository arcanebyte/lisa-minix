# Minix on the Lisa: kernel and drivers

Status: 15 September 2026. Minix 1.5 boots in LisaEm (1 MB and 2 MB) from
a raw ProFile image and runs `login` and the shell on the Lisa's screen
and keyboard and, at the same time, on serial port B, with the root file
system either in a RAM disk (`make lisaem-minix`, phase 2) or on the
ProFile (`make lisaem-hd`, phase 3). User processes run under the Lisa
MMU, each protected from the others (phase 5). **Phase 2, 3, 4 and 5 exit
tests passed in LisaEm.** Not yet run on a real Lisa.

## Building and running

```
make lisa-minix      # build/lisa-minix/minix.image
make lisaem-minix    # boot it in LisaEm and run the test
```

`make lisa-minix` builds the whole tree again with `-DMACHINE=LISA` into
`build/lisa-minix` (library, kernel, mm, fs, init and the root file system
commands), puts kernel, mm, fs and init together with `tools/build.py
--lisa`, appends the root file system from `rootfs/lisa.proto`, and wraps
the result with `boot/lisaboot.S` in a 5 MB ProFile image with
`tools/mklisa.py`. For a real Lisa, copy `minix.image` to the ESProFile
SD card as `profile.image`. The console is the Lisa's screen and keyboard,
and also a 9600 baud terminal (8N1) on serial port B.

The Atari build (`make`, `make hatari-shell`) is unchanged and still
passes.

## What is Lisa-specific

| File | Role |
|---|---|
| `src/include/minix/config.h` | `MACHINE` can be set by the build; `LISA` (63) selects `CHIP == M68000`, one disk drive (the ProFile) |
| `src/kernel/lisa/lisampx.s` | vectors, start-up, save/restart, interrupt entry; from `stmpx.s` |
| `src/kernel/lisa/lisamain.c` | `main`, traps, panic, Lisa start-up, interrupt handlers, stub tasks; from `stmain.c` |
| `src/kernel/lisa/lisacons.c` | console: TTY glue, output to screen and SCC port B, serial input; replaces `stcon.c`, `rs232.c` |
| `src/kernel/lisa/lisavdu.c` | screen terminal (phase 4); from `stvdu.c` |
| `src/kernel/lisa/lisakbd.c` | COPS keyboard (phase 4); replaces `stkbd.c` |
| `src/kernel/lisa/lisapro.c` | ProFile disk task (phase 3) |
| `src/kernel/lisa/lisafloppy.c` | Sony 400K floppy task, `/dev/fd0` (phase 6) |
| `src/kernel/lisa/lisammu.c` | MMU context 1 for user mode (phase 5); replaces `stshadow.c` in use |
| `toolchain/lisa-kernel.ld` | kernel link script: `minix.ld` plus `.setup` where address bit 14 is set (phase 5) |
| `src/kernel/lisa/lisaaddr.h` | hardware addresses, with sources |
| `boot/lisaboot.S`, `tools/mklisa.py` | boot block and image (phase 1) |
| `tools/build.py --lisa` | image layout (below) |
| `rootfs/lisa.proto` | root file system |
| `tools/lisascreen.py` | reads the text on the Lisa screen from a LisaEm screen dump (tests) |

Changes to shared files, each under `MACHINE == LISA`:

- `kernel/const.h`, `kernel/table.c`, `kernel/system.c` (`SIGSTKFLT`):
  treat the Lisa like the ST (shadowing queue, task stack sizes).
- `kernel/system.c` `do_mem`: memory is logical 0 to the end of RAM
  reported by the boot ROM, less the top 32 KB (video page).
- `kernel/tty.h`: no RS232 lines (`NR_RS_LINES` 0).
- `kernel/clock.c`: `init_clock` does nothing; the vertical retrace
  interrupt is the clock.
- `fs/main.c`: the RAM disk is already loaded (below); `fs/glo.h`
  declares the extra word.
- `include/minix/const.h`: no `ABS`, as on the ST.
- Phase 5 (MMU; mapping scheme in `docs/memory-map.md`):
  - `kernel/system.c` `do_fork`: copies the parent's whole image to the
    memory MM allocated and moves the child's map there, instead of
    `mkshadow`. `umap`: physical = virtual - `mem_vir` + `mem_phys`, as on
    the PC, instead of physical = virtual.
  - `kernel/proc.c` `cp_mess`: message pointers are translated from each
    process's virtual addresses.
  - `mm/exec.c`: programs get virtual address 0; no relocation.
  - `mm/forkexit.c`: fork allocates room for text as well, and records the
    child's physical addresses.
  - `mm/alloc.c`: even numbers of clicks at even addresses (512-byte MMU
    pages).
  - `mm/signal.c`: SIGSEGV and SIGBUS from the kernel cannot be caught or
    ignored.
  - `tools/build.py --lisa`: kernel, MM, FS and INIT padded to 512 bytes.
  - `stshadow.c` is still linked (its functions are called from code paths
    that no longer have shadows to handle) but never makes a shadow.

### Start-up and interrupts (`lisampx.s`)

- The ST's vector table put each vector's trap number in the high byte of
  the handler address and got it back from a return address. The Lisa
  version gives every vector its own stub, which stores the number
  explicitly, so nothing depends on the CPU keeping the high byte of the
  PC.
- Interrupt levels (UniPlus `sys/ivec.s`): 1 vertical retrace and parallel
  port VIA, 2 COPS, 3-5 expansion slots, 6 SCC, 7 NMI. Levels 1, 2 and 6
  go through the ST's `async` path to `lisa_level1`, `lisa_level2` and
  `lisa_level6`; the others panic.
- The ST ran with interrupt mask 2 (to block its horizontal blank
  interrupt). On the Lisa, level 1 is the clock and level 2 the keyboard,
  so `unlock()`, tasks and user processes use mask 0.

### Clock

`HZ` is 60. `lisa_level1` acknowledges the vertical retrace interrupt
(write 0xFCE018 then 0xFCE01A, as UniPlus `sys/l1.c`) and calls
`clock_handler`. The two writes run with interrupts masked, only
because LisaEm can otherwise take a pending interrupt in the middle of the
VRT_ON write and corrupt the stack (`docs/lisaem.md`). Both VIAs are
masked at start-up; the keyboard driver then enables the COPS VIA's data
interrupt.

### Console

The console is mirrored: everything written goes to the Lisa screen and to
serial port B, and characters typed on the Lisa keyboard or received on
port B both reach it. PLAN.md asked for a choice at boot; mirroring
makes the choice unnecessary, costs only the polled serial output (about
1 ms a character at 9600 baud), and works with nothing connected to port B
because the SCC is set up without hardware flow control.

#### Serial port B

Serial port B, 9600 baud, 8N1 (Z8530 time constant 11 from the 4 MHz
clock). Output is polled in `out_char`, which also turns newline into CR LF
when `CRMOD` is set (the ST's screen driver did that). Input interrupts
(level 6) queue characters with `kbdput` and flush them to the TTY task as
`stkbd.c` does. `putc` sets the SCC up on first use so kernel `printf`
works from the start.

`rs_flush` is copied from `rs232.c` with one fix: the original returns
without `restore()` when there is nothing to flush, leaving interrupts
locked. The ST code has the same bug; it is not fixed there.

#### Screen (`lisavdu.c`, phase 4)

- Placement as UniPlus `sys/bm.c`: the 32 KB video page is the top of the
  RAM the boot ROM reports (logical `MEMEND - 0x8000`), and the video
  latch (0xFCE800) gets its physical address divided by 32 KB, physical
  being logical plus the ROM's `MEMBASE`. `lisa_user_mem_end()` leaves the
  page out of Minix's memory. Where the video page was before (LisaEm
  starts with it 64 KB below the top) becomes ordinary memory.
- 720 x 364 pixels, 90 bytes a line, a set bit black. Characters are 8 x 9
  pixel cells: the ST's 8 x 8 font from `stfnt.c` and a blank line, 90
  columns by 40 rows, the same as UniPlus. The four scan lines below row 40
  and the rest of the page are filled black, as UniPlus does.
- Black text on white. Escape sequences are `stvdu.c`'s VT100 subset
  (cursor movement and position, erase in line and display, insert and
  delete line and character, reverse video, save and restore cursor,
  index and reverse index, DECCKM, DECKPAM); the ST's colour, 25/50-line
  and sound sequences are gone. BEL does nothing yet.
- The cursor is an inverted cell. `vducursor` keeps track of whether it is
  shown, so kernel `printf` and the TTY task can hide and show it in any
  order.

#### Keyboard (`lisakbd.c`, phase 4)

- The COPS VIA's CA1 interrupt (COPS has a byte) is enabled and nothing
  else; the rest of that VIA stays as the boot ROM set it. `lisa_level2`
  calls `lisa_cops_int`, which reads one byte if the VIA's interrupt flag
  register shows CA1; the COPS interrupts again for the next byte. (A
  first version read while CA1 showed, and could hang LisaEm; see
  `docs/lisaem.md`.)
- Protocol from UniPlus `include/sys/cops.h`, `keyboard.h` and `sys/kb.c`:
  bit 7 is key down, bits 0-6 the key code; `0x80` announces a reset code
  (keyboard or I/O COPS failure, unplugged, clock timer, soft power switch,
  keyboard ID, or a clock reading followed by 5 more bytes); `0x00`
  announces two bytes of mouse movement; codes 1-8 are disk, plug and mouse
  button events. All but keys are read and ignored.
- US key layout tables written from UniPlus's key code facts. As in
  UniPlus, Command is Control, both Option keys send ESC, Enter sends
  newline, and the arrow keys send `ESC [ A`-`D` (`ESC O` in cursor key
  mode). Clear sends ESC and Shift-Clear DEL (Minix's interrupt character);
  UniPlus sent DEL for Clear, but LisaEm types the host's Escape key as
  Clear, and Escape in an editor must not interrupt it. Alpha Lock only affects
  letters.
- Auto-repeat: a key held down repeats after 0.4 s, 15 times a second,
  counted in clock ticks by `kb_timer` (called from the clock interrupt), as
  in the ST driver. `tests/lisaem-console.sh` holds the A key down through
  LisaEm and checks the repeats.
- Characters go to the same `tty_driver_buf` as serial input, so the TTY
  task sees one console.

### Real-time clock (phase 6)

The clock task's `init_clock` sends the COPS the read-clock command
(0x02, `lisa_read_rtc` in `lisakbd.c`, protocol from UniPlus
`l2copscmd`). The answer comes back through the keyboard byte stream: reset
code 0xE0 + year, then five bytes of BCD nibbles (day of the year 1-366,
hour, minute, second, tenth). `lisakbd.c` turns it into seconds since 1970
and `clock_set_boot_time()` (`clock.c`) sets the time of day, so `date` is
right from boot.

- The COPS keeps the year as one nibble, 1980-1995. Minix shows what the
  clock holds; LisaEm always holds 1987.
- The time is not written back: `date` changes Minix's time only.
- LisaEm used to show the date one day early: it filled the day from the
  host's 0-based day of the year, while UniPlus (citing the Lisa Hardware
  Manual) counts 1-366. Fixed in LisaEm (#75, September 2026); still to be
  checked on a real Lisa.
- Tested by `tests/lisaem-console.sh`, which types `date` and compares the
  time of day with the Mac's.

### Sony floppy (phase 6)

`src/kernel/lisa/lisafloppy.c` is the floppy task (major 2, `/dev/fd0`,
the whole 400K disk). The Lisa 2's drive is run by a 6504 on the I/O board;
the driver talks to it through shared RAM at 0xFCC000 (one byte at each odd
address), from UniPlus `include/sys/sony.h`, `sys/sony.c` and `sys/l1.c`
and LisaEm's `floppy.c`:

- To read or write a sector: wait until the controller is ready (DSKDIAG,
  bit 0x40 of the parallel VIA's port B, and the go byte back to 0), set
  drive 0x80, side 0, track and sector, function 0 (read) or 1 (write,
  with the 512 data bytes and 12 tag bytes in shared RAM), wait again, and
  write 0x81 to the go byte.
- The controller interrupts on level 1 with FDIR (bit 0x10 of the COPS
  VIA's port B) set. `lisa_level1` asks `lisa_fd_int` first, as UniPlus
  `l1intr` does, and only a level 1 interrupt without FDIR is a clock tick.
  The handler takes the status and, for a read, the data, acknowledges
  (function 0x77, go 0x85) and wakes the task. Disk-inserted and eject
  button interrupts are acknowledged and ignored.
- Block numbers map to track and sector across five zones of 16 tracks
  with 12 to 8 sectors per track. Each request is retried 3 times; a
  10-second clock alarm catches a controller that never answers. No disk
  in the drive gives EIO and "Floppy: no disk in the drive".
- Not done: eject (the disk comes out when LisaEm or the Lisa's button
  ejects it; Minix does not send the eject command), formatting, 800K
  drives, the Lisa 1's Twiggy drives, disk-change detection beyond "no
  disk".

`tests/lisaem-floppy.sh` (`make lisaem-floppy`), 15 September 2026, LisaEm
1 MB: a DC42 disk made on the Mac with a Minix file system and one file is
inserted while Minix runs; `mount /dev/fd0 /mnt`, `cat` of the file,
copying `/etc/motd`, `kermit` and `elvis` onto it (147 of 400 blocks) and
`umount`, all typed on the Lisa keyboard; on the Mac the disk has every
file byte for byte and checks clean. **Passed in LisaEm.** Also run by hand the
same day: `mkfs /dev/fd0 400` on a blank disk, mount, copy, remount, read
back; the disk checks clean on the Mac.

One bug on the way: the task waited for the controller with
`receive(HARDWARE, &mess)`, the same global buffer that held the request,
so a single-block read lost its message type and was never copied to FS.
Reads through FS's read-ahead (scattered I/O, which copies the request)
worked, which made `dd` succeed while `mount` failed.

### Commands (phase 6)

`/bin` on the hard disk image has every single-file command in
`src/commands` and the multi-file ones: `ar`, `bawk`, `de`, `elvis`
(with `ctags`, `ref`, `virecover`), `ic`, `indent`, `kermit`, `m4`, `make`,
`mdb`, `mined`, `nroff`, `patch`, `rz`, `sz`. The Makefile builds each from
the object list and flags of its own Makefile (`MCMD_*`, `MDEFS_*`).
Changes to the sources: declarations gcc needs (`elvis/regexp.h`,
`elvis/cut.c`), and declarations of pointer-returning functions that were
missing (`docs/toolchain.md`). `mined` is built with `-DUNIX`, its termcap
version, so it uses all 40 lines.

`/etc/termcap` describes the console as `minix` (the `TERM` that `login`
sets): 90 x 40, the escape sequences `lisavdu.c` understands; and `vt100`
for a terminal on serial port B. `/usr/tmp` exists for elvis.

`tests/lisaem-cmds.sh` (`make lisaem-cmds`), 15 September 2026, LisaEm
1 MB, typed on the Lisa keyboard: `make`, `m4`, `bawk`, `nroff`, `patch`
(against `diff` output), `ar` and `indent` give the expected output;
`kermit` starts and reports its version; elvis (`i`, text, Escape, `:wq`)
and mined (text, ^W, ^X) write files that the Mac reads back. **Passed.**

### Image and RAM disk

`tools/build.py --lisa` writes kernel, mm, fs and init one after the other
from address 0, bss included, each padded to a click (256 bytes), with
the ST's patches: sizes in the kernel's data, init's origin and size in
FS's data. It appends the root file system at the end of init and writes
its size in blocks to FS's data (`data_org[INFO + 3]`). The boot block
loads the whole thing. MM allocates the RAM disk right after init, which
is exactly where the file system already is, so FS only tells the memory
driver its size and does not copy anything.

Memory, 1 MB LisaEm, phase 2: `Memory size = 960K  MINIX = 127K  RAM disk = 360K
Available = 473K`. 2 MB (`--allow2mbram`): 1856K, 1369K available.

## ProFile driver (phase 3)

`src/kernel/lisa/lisapro.c` is the hard disk task for the ProFile on the
built-in parallel port (devices and layout: `docs/disk-layout.md`). It is
written from the ProFile protocol as described in Apple's "ProFile HD
Communications Protocol" and the UniPlus project's notes, with the
register sequence checked against UniPlus `stand/pro.c` (read, not
copied).

- **Polled:** no VIA interrupts. Each phase is a handshake on /CMD and
  /BSY: the drive's state byte (1 command, 2 read, 3 write, 6 perform), the
  host's $55, then /BSY rising when the drive is done. All bytes go through
  VIA register 1, which strobes CA2 with PCR $6B.
- **Blocks:** one ProFile block per request: 6 command bytes; read returns
  4 status, 20 tag and 512 data bytes; write sends 20 tag (zero) and 512
  data bytes. Status byte 1 bits $DD or byte 3 bit $80 count as an error;
  each block is tried up to 5 times.
- **Start-up:** reads the spare table for the disk size and block 1 for the
  file system area, and prints both.
- **Timeouts:** 200,000 polls of /BSY per wait, which is generous in LisaEm;
  to be checked on a real ProFile and ESProFile.

With the root on `/dev/hd0`: `Memory size = 960K  MINIX = 129K  RAM disk =
0K  Available = 831K` (phase 3). Since phase 4 the video page is 32 KB, not
64 KB, and the screen code is in the kernel: `Memory size = 992K  MINIX =
136K  Available = 856K` at 1 MB, `1888K`, `1752K` available at 2 MB.

Phase 3 exit test (`make lisaem-hd`), 14 September 2026, LisaEm 1 MB:
boot 1 logs in, finds 137 commands in `/bin`, writes `/tmp/hello`,
`sync`s and runs `fsck /dev/hd0` (no errors); boot 2 on the same image
reads the file back and `fsck` again reports no errors with the same
counts; on the Mac, `tools/minixfs.py` reads the file and checks the file
system clean (6868 free zones, as Minix `fsck` reported). **Passed in
LisaEm.**

Phase 4 exit test (`make lisaem-console`, `tests/lisaem-console.sh`), 14
September 2026, LisaEm 1 MB and 2 MB, everything typed on the Lisa keyboard
(through LisaEm's `LISAEM_KEYBOARD_FILE`): log in, `ls /bin`
(137 lines, scrolls the screen), `echo` every shifted symbol into a file,
`cat > file` ended with Command-D, `sync; cat` it back. On the Mac, both
files hold exactly what was typed, the screen dump read by
`tools/lisascreen.py` ends with the last command, its output and the
prompt, and the file system checks clean. **Passed in LisaEm.** Two
characters are left out of the typed test: `@`, which is Minix's line-kill
character, and `|`, which LisaEm types on the wrong key (`docs/lisaem.md`).

## MMU (phase 5)

The mapping scheme is in `docs/memory-map.md`. In short: supervisor mode
(kernel, tasks) uses MMU context 0 as the boot ROM left it; user mode uses
context 2 for MM, 3 for FS and 1 for everything else (INIT, programs,
IDLE), which `restart` reloads from the process's memory map when a
different process is about to run.
Programs run at virtual address 0; fork copies the whole image; a stray
access is a bus error and SIGSEGV, which cannot be caught.

Debugging aid added on the way: control-T typed on either console input
prints the Minix-ST process table and memory map dumps (`stdmp.c`).

### Exit test

`tests/lisaem-mmu.sh IMAGE [MEM_KB]`, on `build/lisa-minix/minix-hd.image`,
which now has `/usr/test` with the Minix system call tests (`src/test`,
built `-O0`), `forkbench`, `segv` and the script `phase5`. The same test
was run on a phase 4 (shadowing) kernel built from the same tree with the
same test programs, in the same LisaEm build, 15 September 2026, 1 MB:

| | Shadowing (phase 4) | MMU (phase 5) |
|---|---|---|
| test0-test19 | all ok | all ok (1 MB and 2 MB) |
| test20, test21 (file permissions) | fail | fail the same way |
| `segv`: write to 0x80000, outside itself | "survived" (wrote into another process) | killed: `sig=11 ... Memory fault - core dumped` |
| after `segv` | system runs, `fsck` clean | system runs, `fsck` clean |
| system call tests, total | 1008 s | 460 s |

`forkbench 20` (seconds of host time from the serial log, 20 iterations):

| Loop | Shadowing | MMU |
|---|---|---|
| fork, child exits | 0.8 | 1.3 |
| fork, child writes 32 KB of data first | 34.2 | 14.4 |
| fork, child execs `/bin/echo` | 2.6 | 4.1 |

Per test, the fork-heavy tests are much faster (`test1` 129 s to 20 s,
`test2` 106 to 23, `test4` 105 to 14, `test13` 324 to 28) and the others
up to 2.5 times slower (`test6` 13 to 32 s, `test9` 11 to 26). With
shadowing, a fork is cheap until parent and child both run, and then
every switch between them copies their data and stack; with the MMU a
fork always copies the whole image, text included, and every switch
between user processes reloads context 1. MM and FS are user processes, so
most system calls cost two reloads, and in LisaEm each MMU register write
also discards cached decoded instructions. Running IDLE in supervisor mode
so that waiting for the disk costs no reload was tried and made no
measurable difference, and was not kept. Giving MM and FS contexts of their
own (2 and 3) did: the test suite went from 460 s to 356 s, and `test6`
and `test9` from 32 and 26 s to 16 and 13 s, close to shadowing's 13 and
11 s (same day, image with the multi-file commands added). The Minix clock also kept better
time: with shadowing, `forkbench` counted 26 s for a loop that took 34 s,
because interrupts are locked while images are exchanged.

Also checked with the MMU kernel: phase 4 console test (1 MB and 2 MB),
phase 3 ProFile test, phase 2 RAM disk boot, and the Atari build.

Two problems found while testing were not in the kernel: a LisaEm crash
when code runs in logical page 0 (fixed in LisaEm, #72,
`docs/lisaem.md`), and gcc optimizing test1's signal wait and a benchmark
loop into endless loops (`docs/toolchain.md`).

## Not done yet

- MMU: text is not write-protected; MM, FS, INIT and INIT's children
  before exec can reach memory below them in their first segment.
- No printer, no second serial port, no mouse, no sound (BEL),
  only the US keyboard layout. The ProFile driver supports only the built-in port.
- Built but not tested in LisaEm: `de`, `ic`, `mdb`, `rz`, `sz`, `ctags`,
  `ref`, `virecover`. Not built: the ST-specific directories (`atari`,
  `stclock`, `stterm`).
- The video page follows UniPlus and works in LisaEm; to be checked on a
  real Lisa, as are the COPS VIA settings the boot ROM leaves.
- Serial input in LisaEm is slow on purpose (`SCC_MIN_CYCLES_BETWEEN_READS`
  in `z8530.c`); `tools/lisaem_run.py --send` types one character every
  0.3 s.

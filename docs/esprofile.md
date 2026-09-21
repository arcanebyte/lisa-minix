# Minix on a real Lisa with ESProFile

Status: 15 September 2026. **Not yet tried on real hardware.** Everything
below has been checked only in LisaEm; this page is the plan and checklist
for the first real boot, and the place to record what happens.

## What to put on the SD card

ESProFile (`~/github/ESProFile`, `sw/ESProFile/ESProFile_Emulator.ino`)
boots the file named `profile.image` in the root of its SD card, and knows
only two sizes, 5,175,296 and 10,350,592 bytes. Both Minix images are
exactly one of those (`docs/disk-layout.md`).

| Image | Build | Size | Root | Good for |
|---|---|---|---|---|
| `build/lisa-minix/minix.image` | `make lisa-minix` | 5 MB | RAM disk, loaded by the boot block | first test: needs only reads from the ProFile, never writes to it |
| `build/lisa-minix/minix-hd.image` | `make lisa-minix-hd` | 10 MB | `/dev/hd0` on the ProFile | the full system: all commands, elvis, `/usr/test` |

1. **Rename the card's existing `profile.image` first** (for example to
   `selector.image`). If it is the ESProFile Selector, replacing it
   removes the Selector (ESProFile's "Selector rescue" can restore one
   from its rescue folder, if that folder was set up).
2. Copy the Minix image to the card as `profile.image`.
3. Keep a copy of the image on the Mac: after running Minix, the card's
   copy can be checked there with `python3 tools/minixfs.py profile.image
   check` and read with `ls` and `get`.

## The Lisa

- At least 1 MB of RAM.
- ESProFile on the **built-in parallel port**. That is the only port the
  Minix ProFile driver (`src/kernel/lisa/lisapro.c`) and the boot block
  use. On a Lisa 2/10 the internal Widget is on that port, so ESProFile
  has to take its place there; dual parallel cards are not supported.
- A US keyboard (`src/kernel/lisa/lisakbd.c` has only that layout).
- Optional but useful: a 9600 baud, 8N1 terminal or USB serial adapter on
  **serial port B**. Minix mirrors its console there, so kernel messages
  can be captured even if the screen shows nothing.

## Booting

Power on and choose the ProFile at STARTUP FROM (or let the Lisa boot it
if it is the startup device). Expected, on the screen and on port B:

```
ProFile: 19456 blocks; file system at block 282, 16000 blocks    (10 MB image)
Booting MINIX 1.5.  Copyright 1991 Prentice-Hall, Inc.
Memory size =  992K     MINIX = 140K     RAM disk =    0K     Available = 852K
Minix root file system loaded.
login:
```

The 5 MB image prints a RAM disk size instead of the ProFile line. Log in
as `root` (no password).

## Tests, in order

Each step matches a LisaEm test; stop at the first that fails and note
what was on the screen and on port B.

| # | Do | Expect | LisaEm test |
|---|---|---|---|
| 1 | boot the 5 MB image | banner and `login:` on screen and port B | `make lisaem-minix` |
| 2 | type `root`, `ls -l /` | listing; typed characters echo on the Lisa screen | `make lisaem-console` |
| 3 | `date` | the Lisa clock's time of day (see clock below) | `make lisaem-console` |
| 4 | boot the 10 MB image; `ls /bin \| wc` | 155 commands | `make lisaem-hd` |
| 5 | `echo hello > /tmp/x; sync; fsck /dev/hd0` | no errors | `make lisaem-hd` |
| 6 | power off, boot again, `cat /tmp/x` | `hello` | `make lisaem-hd` |
| 7 | `/usr/test/segv` | `Memory fault - core dumped`, then the shell prompt | `tests/lisaem-mmu.sh` |
| 8 | `elvis /tmp/e`, type, Escape (Clear or an Option key), `:wq` | file written | `make lisaem-cmds` |
| 9 | `sh /usr/test/phase5` (takes several minutes) | every `Test N ok` except 20 and 21 | `tests/lisaem-mmu.sh` |
| 10 | with a spare 400K disk in the drive: `mkfs /dev/fd0 400`, `mount /dev/fd0 /mnt`, `cp /etc/motd /mnt`, `umount /dev/fd0`, mount again and `cat /mnt/motd` | the file back; no "Floppy:" errors | `make lisaem-floppy` |

Control-T (Command-T) prints the kernel's process table, which helps if
something hangs. Before powering off, type `sync`.

## What has only been checked in LisaEm

Each of these is a place where the real machine could differ. The file to
look at is given for each.

1. **Boot ROM read routine.** The boot block calls the ROM's ProFile read
   routine at 0xFE0090 with the register protocol from the UniPlus Priam
   booter notes, checked in LisaEm with boot ROM H. Another ROM revision,
   or a difference in which registers the routine preserves, would stop
   the boot before any output. (`boot/lisaboot.S`)
2. **MMU setup mode.** Register writes happen from code and data placed
   where address bit 14 is set, as LisaEm and UniPlus indicate. If this is
   wrong the kernel stops right after the first lines of output, when the
   first user process starts. (`src/kernel/lisa/lisammu.c`,
   `toolchain/lisa-kernel.ld`)
3. **ProFile timing.** The driver polls /BSY up to 200,000 times per wait
   and retries a block 5 times. ESProFile answers faster than a real
   ProFile; a timeout shows as ProFile errors in the log.
   (`src/kernel/lisa/lisapro.c`, `BSY_WAIT`)
4. **Video page.** Minix moves the screen to the top 32 KB of RAM and sets
   the video latch as UniPlus does. A blank or scrambled screen with
   normal output on port B points here. (`src/kernel/lisa/lisavdu.c`)
5. **Keyboard.** Only the COPS VIA's data interrupt is enabled; the rest of
   that VIA is left as the boot ROM set it. No typing, but output working,
   points here. (`src/kernel/lisa/lisakbd.c`)
6. **Clock.** The read-clock command waits up to 1000 polls for the COPS to
   become ready; "Lisa clock: the COPS did not take the read command" at
   boot means the handshake timing needs changing. The year is 1980 plus
   the COPS year nibble. Minix counts days of the year from 1, as
   UniPlus does (citing the Hardware Manual), and so does LisaEm since
   #75. If the real Lisa's `date` disagrees with the Lisa Office System's
   clock by a day, this is the place to look. (`lisakbd.c`
   `rtc_reading`)
7. **Serial port B.** 9600 baud assumes the SCC's 4 MHz clock. Garbage on
   the terminal means the time constant is wrong. (`src/kernel/lisa/lisacons.c`)
8. **Clock tick.** The vertical retrace interrupt is acknowledged as in
   UniPlus. If it does not tick, `sleep` never returns and `date` does not
   advance. (`src/kernel/lisa/lisamain.c` `lisa_level1`)
9. **2 MB.** Only LisaEm's 2 MB configuration (2 MB less 128 KB, RAM at
   physical 0) has been tried; a real 2 MB Lisa's RAM layout may differ.
   (`docs/memory-map.md`)
10. **Floppy.** The controller handshake (DSKDIAG, go byte) and the level 1
   FDIR check follow UniPlus and work in LisaEm, which runs the controller
   instantly; a real drive takes seconds to seek, and the driver gives up
   after 10. (`src/kernel/lisa/lisafloppy.c`)
11. **Soft power switch.** Minix ignores the COPS's "power switch pressed"
   report, so the front button does not shut Minix down.

## Results

| Date | Lisa (model, ROM, RAM) | ESProFile | Image | Steps passed | Notes |
|---|---|---|---|---|---|
| | | | | | |

# Minix 1.5 on the Apple Lisa: plan

Status: plan only, nothing built (14 September 2026).

## 1. Goal

Boot Minix 1.5 on an Apple Lisa 2, in LisaEm and on real hardware
through ESProFile, starting from the Atari ST port, which is the only
68000 port that ran on bare hardware. End state for this plan: Minix
boots from a ProFile image that is bootable on its own (no chain
loader), runs a shell on the Lisa's own screen and keyboard, and uses
the Lisa MMU for per-process relocation and protection. Networking is
out of scope (section 9).

Category, in the UniPlus project's terms: Minix 1.0 is 1987 and the
68000 ports are 1989–1990, so this is **PARTLY** period correct at
best. A 1990 Lisa owner could have done it; nobody did. It is a
separate project from the UniPlus restoration and shares no code with
it, only hardware knowledge.

Why Minix rather than Linux: Linux/m68k needs a 68020 with a paging
MMU, and uClinux needs a 2.0-era kernel and gives up fork and
protection. Minix 1.5 was written for exactly this class of machine: a
68000 with no restartable bus errors, no paging, 1–2 MB of RAM, and a
few hundred KB of kernel. See the conversation notes in section 10.

## 2. Sources and licenses

| Source | Where | Use |
|---|---|---|
| Minix-ST 1.5 | https://github.com/EmmanuelKasper/minix-st-1.5 (archived), from www.subsole.org/minix_on_the_atari_st | Base tree. `src/kernel` has the 68000 core (`stmpx.s`, `proc.c`, `system.c`, `stshadow.c`) and the ST drivers (`st*.c`). |
| MacMinix 1.5 | https://github.com/macminix/MacMinix | Reference for a second 68000 port; `distrib/` may hold ACK binaries useful for self-hosting later. Ran under Mac OS, so its drivers are not useful. |
| Minix 1.5 reference manual (Prentice Hall, 1990) | Book | Kernel internals; the book edition matches this source. |
| Lisa Hardware Manual (Apple, 1983), Bitsavers | bitsavers.org/pdf/apple/lisa | MMU, VIAs, SCC, COPS, video, boot ROM protocol. |
| UniPlus V.1.5+ sources, `~/github/uniplus/v1.5` | Local | Working Lisa drivers to crib register sequences from: `sys/pro.c` and `stand/pro.c` (ProFile), `sys/scc.c` (serial), `sys/kb.c` + `include/sys/cops.h` (keyboard), `sys/tt0.c` + `sys/vt100.c` + `include/sys/bmfont.h` (bitmap console), `sys/sony.c` (floppy), `sys/clock.c`, `sys/machdep.c` + `include/sys/mmu.h` (MMU), `stand/pbblk.s` + `stand/boot.c` (boot block and loader). UniSoft code is proprietary: read it, do not copy it. |
| LisaEm, `~/github/lisaem` | Local | Emulator, tracing, ProFile and EtherBox models. `src/lib/libdc42/src/lib_raw_profile_image.c` reads the raw `.image` format; `src/lisa/cpu_board/romless.c` shows the ROM's ProFile boot check; `src/host/wxui/lisaem_wx.cpp` has the RAM layout per memory size. |
| ESProFile, `~/github/ESProFile` | Local | ESP32 ProFile emulator used on the real Lisa. `sw/ESProFile/ESProFile_Emulator.ino` defines the SD card image format and the `profile.image` boot file. |
| UniPlus `~/github/uniplus/tools/make_profile_image.py` | Local | Working writer for raw ProFile images (our own code); model for `tools/mklisa.py`. |

License: Minix 1.x sources were relicensed BSD 3-clause in April 2000,
retroactively. Keep the Minix copyright and license file in the repo.
Anything derived from UniSoft sources stays out of this repo.

## 3. Hardware facts the plan depends on

- CPU: 68000 at 5 MHz. Bus errors are not restartable, so the kernel
  must never take a fault it intends to recover from. Minix already
  assumes this.
- RAM: **1 MB minimum, 2 MB supported**; 512 KB Lisas are not
  supported. Test at both 1 MB and 2 MB. Physical RAM does not start
  at 0: it ends at the 2 MB boundary and starts below it, and the ROM
  maps logical 0 onto the lowest physical RAM. In LisaEm
  (`lisaem_wx.cpp:9924`): 1 MB is physical 0x080000–0x180000; 2 MB is
  0x000000–0x1E0000 with `--allow2mbram` or 0x000000–0x200000 with
  `--full2mbram`, neither of which is in a default build; 1.5 MB is
  marked "causes crashes". LisaEm's own comment calls these layouts
  "totally wrong", so confirm against the hardware manual. The video
  page is the top 64 KB of RAM (`videolatchaddress = maxlisaram -
  0x10000`). The kernel must take the physical base from the segment
  registers, never assume it.
- MMU: 128 segments of 128 KB in a 16 MB logical space, each with a
  512-byte-granular origin and limit and an access type (read-only,
  read-write, stack variants, I/O, invalid). Four contexts selected by
  the SEG1/SEG2 bits; context 0 is what the ROM leaves the kernel in.
  Registers and bit values: UniPlus `include/sys/mmu.h` (standard I/O
  space at 0xFC0000, special I/O at 0xFE0000, setup mode, segment
  origin and limit registers).
- I/O: two 6522 VIAs (keyboard COPS, parallel port), Zilog 8530 SCC
  (two serial ports), 720×364 one-bit framebuffer at an address set by
  the video latch, 6504-based floppy controller with a shared RAM
  window, ProFile on the built-in parallel port and on dual parallel
  cards in slots.
- Boot: the ROM reads ProFile block 0, putting the 20 tag bytes at
  0x1FFEC and the 512 data bytes at 0x20000. It refuses to boot unless
  **tag bytes 4–5 are 0xAAAA** (`romless.c:577`), then calls 0x20000
  with the address of the ROM's read-block routine on the stack. The
  read routine takes d1 = block, d2 = timeout, d3 = retries, d4 =
  threshold, with a0 restored to its entry value before each call
  (`stand/pbblk.s`, the Priam booter; confirm the ProFile case by
  tracing). The boot block must be position independent.
- Disk image format: raw `.image`, blocks of 532 bytes (20-byte tag,
  then 512 data bytes) in drive block order, block N at file offset
  N×532. The same file works in LisaEm and on ESProFile. Size must be
  exactly 5,175,296 bytes (9728 blocks) or 10,350,592 bytes (19456
  blocks): ESProFile sets its spare table from the file size and only
  knows those two.
- ESProFile boots the file named `profile.image` on its SD card. The
  Minix image is copied there under that name, so the ROM reads the
  Minix boot block directly, with no Selector in between.
- Emulator: LisaEm with the faithful ProFile emulation from lisaem
  PR #55, which is what boots UniPlus today. Serial port B can go to
  a File, Pipe, TelnetD or PseudoTTY; port A offers only Nothing or
  Loopback unless LisaEm is built with `ALLOWSERIALA`
  (`LisaConfigFrame.cpp:176-187, 1002`).

## 4. What Minix-ST gives us

Tree layout (`src/`): `kernel`, `mm`, `fs`, `lib`, `include`,
`commands`, `tools`, `test`. The kernel is 8.8 K lines, of which the
machine-independent part (`proc.c`, `system.c`, `tty.c`, `clock.c`,
`memory.c`) is about 3 K and the ST-specific part (`st*.c`, `rs232.c`,
`stmpx.s`, `copy68k.s`, `stdskclks.s`) about 5 K.

Key properties:

- `CHIP == M68000`, `MACHINE == ATARI`; the machine switch is in
  `include/minix/config.h`. We add `MACHINE == LISA`.
- Memory is allocated in 256-byte clicks (`CLICK_SIZE 256`). No
  swapping in 1.5: every process must fit in memory.
- No relocation hardware is assumed. `fork()` is handled by
  **shadowing** (`kernel/stshadow.c`): the child is copied to another
  physical location and copied back into the parent's addresses when
  it runs. It works but is slow. On the Lisa the MMU makes this
  unnecessary (phase 5).
- The boot image is built by `tools/build.c`: boot block, then kernel,
  mm, fs, init and menu concatenated, each padded to a click, with a
  size table patched into the kernel's data. `tools/boot.s` is the ST
  boot block, which reads the image with the TOS BIOS. Ours will use
  the Lisa ROM read routine instead.
- Drivers are tasks that receive messages; disk drivers can be polled
  and synchronous, which suits the ProFile protocol.
- Compiler: everything was built with the Amsterdam Compiler Kit
  (ACK). Assembly files use ACK syntax (`!` comments, `.define`,
  `.sect`, `.data2`). `lib/atari` holds ACK runtime helpers (`_dvi.s`,
  `_mli.s`, ...) that gcc does not need.
- Console: `stvdu.c` (bitmap terminal with a font in `stfnt.c`) and
  `stkbd.c` are the shape of what the Lisa needs.
- Devices in `fs/table.c`: mem/ram, floppy, winchester, tty, printer.
  No network devices; Minix had no TCP/IP until 2.0.

## 5. Decisions

1. **Toolchain: cross gcc on the Mac** (`m68k-elf-gcc -m68000`,
   binutils, `objcopy -O binary`), with a linker script that puts the
   kernel at its load address. Convert the ACK assembly to GNU syntax
   (about 1.1 K lines: `stmpx.s`, `copy68k.s`, `stdskclks.s` only if
   kept, `lib/atari/*.s`, `tools/boot.s`, `tools/type.s`). Drop the ACK
   runtime helpers and link libgcc. Fallback if gcc fights the tree
   (int size, struct layout, `_PROTOTYPE` macros): build ACK itself,
   which still compiles on modern hosts and has a 68000 target. Decide
   at the end of phase 0.
2. **Check the integer model first.** ACK's 68000 targets existed with
   16-bit and 32-bit `int`. Read `include/minix/type.h`, `lib/atari`
   and the `sizes` handling in `stmain.c` to find which one this tree
   assumes, and match it in the gcc flags. This is a blocking check.
3. **Boot from a dedicated raw ProFile image that is bootable on its
   own**, never the UniPlus disk and never through a chain loader or
   the ESProFile Selector. One `.image` file (5 MB or 10 MB, section 3)
   is used unchanged in LisaEm and on ESProFile. Layout:
   - block 0: tag bytes 4–5 = 0xAAAA, data = the boot block (at most
     512 bytes);
   - block 1: a header written by `mklisa.py` with a magic number, the
     boot image's block count and load address, and the filesystem's
     start block and size, so the boot block and kernel need nothing
     compiled in;
   - blocks 2..N: the Minix boot image (kernel, mm, fs, init);
   - the rest: a Minix V1 filesystem.

   Built on the Mac by `tools/mklisa.py`, whose block writer follows
   `tools/make_profile_image.py` from the UniPlus repo, plus a host-side
   `mkfs` (port of `commands/mkfs.c`) and the Minix `build` tool.
   Data-block tags are zero.
4. **Serial console first, bitmap console second.** The console is SCC
   **port B**, which LisaEm can send to a file, pipe or telnet socket;
   on the real Lisa it goes to a terminal on the port B connector. The
   bitmap console and COPS keyboard come once the kernel runs.
5. **Keep shadowing until the system works, then replace it with MMU
   relocation.** Shadowing is proven; MMU use is new code and a Lisa
   invention. Phase 5, not phase 1.
6. **Kernel runs in context 0 with the ROM's mapping** for phases 1–4.
   The ROM maps logical 0 onto the lowest physical RAM (which is not
   physical 0 on most configurations, section 3) and I/O space at
   0xFC0000. The kernel works in logical addresses and reads the
   physical base and RAM size from the segment registers at boot;
   confirm with the hardware manual (phase 1 prints the registers).
7. **All Lisa-side code is new**, written from the hardware manual and
   from reading (not copying) the UniPlus drivers. Comments say which
   register sequence came from where.

## 6. Phases

Each phase has a deliverable and an exit test. "Tested" means observed
in LisaEm or on a Lisa; "built" means it compiled on the Mac.

### Phase 0: baseline and toolchain (no Lisa code)

- Import `minix-st-1.5/src` into `src/` unchanged, with a
  `PROVENANCE.md` giving the commit and origin. Add the Minix license.
- Install the cross toolchain; write `toolchain.md` with exact
  versions and flags.
- Convert the ACK assembly and Makefiles; build the **unmodified
  Atari** kernel, mm, fs, init and a few commands with gcc.
- Run the result under **Hatari**. Exit test: Minix-ST built with our
  toolchain boots to a shell under Hatari. This proves the toolchain
  conversion before any Lisa work, and Hatari stays the reference
  system for "is this a Minix bug or a Lisa bug" questions.
- Deliverables: `src/`, `Makefile` or `build.sh`, `toolchain.md`,
  Hatari notes.

### Phase 1: a kernel image on the Lisa

- `boot/lisaboot.s`: a position-independent boot block, at most 512
  bytes, that sets its own stack, reads the header in block 1, uses
  the ROM's read routine to load the boot image into RAM and jumps to
  it, in the shape of `stand/pbblk.s` and `tools/boot.s`. Load address
  and stack must not overlap the image or the video page. On a read
  error it jumps to the ROM monitor.
- `tools/mklisa.py` (first version): boot block, header and kernel
  image into a 5 MB raw `.image`, with the 0xAAAA tag on block 0.
- `kernel/lisa/` skeleton: vector table, `start`, stack, SCC
  initialisation, a polled `putc`, a timer tick from a VIA timer
  (Minix wants `HZ` 60) with an interrupt handler, and a bus/address
  error handler that prints the exception frame and halts.
- The kernel prints memory size, the contents of the segment
  registers for context 0 and a tick counter.
- Exit test: the one `.image` file boots on its own in LisaEm (at
  1 MB and 2 MB) and on a real Lisa as `profile.image` on ESProFile;
  the serial log shows the banner, segment registers and ticks in
  both. Deliverables: `boot/`, `tools/mklisa.py`,
  `kernel/lisa/lisaaddr.h` (memory map), `kernel/lisa/lisamain.c`,
  `docs/memory-map.md`, `docs/esprofile.md`.

### Phase 2: Minix boots with a RAM disk root

- Wire `MACHINE == LISA`: `lisampx.s` (converted `stmpx.s` with Lisa
  vectors and interrupt dispatch), `lisamain.c` (from `stmain.c`,
  without the TOS memory tricks), `memory.c` RAM disk, `tty.c` over
  the SCC (`rs232.c` reworked for the 8530).
- The boot image carries a small root filesystem for `/dev/ram`
  (Minix 1.5 supports a RAM disk root loaded from the boot device).
- `init`, `sh`, `ls`, `cat` from `commands/`, built with the cross
  toolchain and the converted `lib`.
- Exit test: login prompt on the serial console, `ls /` works,
  `fork` via the shell works (shadowing). Deliverables: kernel with
  `lisa/` directory, `tools/mkimage`.

### Phase 3: ProFile root

- `kernel/lisa/lisapro.c`: a polled ProFile driver as a Minix disk
  task, modeled on the standalone driver in `stand/pro.c` (simpler
  than the interrupt-driven `sys/pro.c`). Partitions: the Minix
  filesystem starts after the boot area.
- Host tools: `tools/mkfs` (port `commands/mkfs.c` to run on the Mac),
  `tools/mklisa.py` extended to add the filesystem, `tools/fsck` on the
  host, and a tool to list and copy files out of an `.image` (for
  round trips through the ESProFile SD card).
- Exit test: root on `/dev/hd0`, full `commands/` set installed,
  `fsck` clean after a reboot, a file written on the Lisa is read
  back on the Mac. Run in LisaEm and on the real Lisa with ESProFile.
  Deliverables: driver, tools, `docs/disk-layout.md`.

### Phase 4: Lisa screen and keyboard

- `lisavdu.c` from `stvdu.c`: 720×364 framebuffer, 8×? font (reuse
  `stfnt.c` or a Lisa-like font), VT52-ish escapes as Minix expects,
  no scrolling tricks beyond what `tty.c` needs.
- `lisakbd.c`: COPS keyboard via VIA, keymap for the Lisa layout,
  modifier handling; mouse ignored.
- Console selection at boot (serial or screen).
- Exit test: shell on the Lisa's own screen and keyboard in LisaEm.

### Phase 5: use the MMU

- Replace shadowing: give each process a context or, simpler, keep one
  user context (SEG1) and reload its segment registers on every
  context switch so that text, data and stack appear at the same
  logical addresses in every process. Then `fork` is a copy to free
  memory with no flipping, and `exec` needs no relocation.
- Add protection: invalid segments outside the process, read-only
  text, kernel space unmapped in user context, so a wild user program
  gets a bus error that the kernel turns into `SIGSEGV` (kill only,
  never restart).
- Exit test: fork-heavy tests from `src/test` pass, a deliberately
  faulting user program is killed and the system continues, timing of
  `fork`/`exec` before and after. Deliverables: `lisammu.c`, doc
  section on the mapping scheme, `PLAN.md` note that this is the
  Lisa-only addition.
- Note: this phase is the one addition that no earlier Minix 1.5 port
  had. Minix-ST ran without memory management hardware; the Lisa's MMU
  makes shadowing unnecessary. Done 15 September 2026: `lisammu.c`, the
  mapping scheme in `docs/memory-map.md`, results in `docs/lisa-kernel.md`.

### Phase 6 and later (unordered)

- Sony floppy driver (`sony.c` is 600 lines of protocol with the 6504;
  hard) for real-hardware installs. Done in LisaEm 15 September 2026:
  400K read and write, `/dev/fd0` (`src/kernel/lisa/lisafloppy.c`); no
  eject, format or 800K.
- Real-time clock and `date` at boot. Done 15 September 2026 (reading;
  setting the COPS clock is not done).
- Other ProFile emulators (IDEFile, ArduinoFile, the pico-profile work
  in `~/github/pico-profile`) with the same raw image. ESProFile itself
  is part of every phase's exit test from phase 1 on.
- Self-hosting: needs ACK for 68000 on Minix; check what MacMinix's
  `distrib/` and the Atari distribution binaries contain.
  Checked 15 September 2026: the Minix-ST source tree has the `cc` driver
  (`commands/atari/cc.c`) but none of the compiler passes. MacMinix's
  `distrib/` is one file, `MacMinix_1.5.10.7.sea.hqx`: BinHex around a
  compressed StuffIt self-extracting archive. `binhex decode` gets the
  archive; listing or extracting it needs a StuffIt extractor such as The
  Unarchiver (`unar`), which is not installed. Next step: extract it and
  see whether the ACK passes are there, and in what executable format.
- Dual parallel card ProFiles, Priam, Widget.
- Networking: none in Minix 1.5. Options later are a port of the
  Minix 2.0 `inet` server (1996, not period) or a small KA9Q-style
  stack as a Minix task, driving the EtherBox with a driver written
  from the 3Com spec. Not planned.

## 7. Open questions and risks

1. **Integer size and struct layout** of the ACK-built tree versus gcc
   (decision 2). Wrong answer means nothing links or the filesystem
   layout differs from the book. Resolve in phase 0.
2. **Serial console.** Resolved for LisaEm: port B routes to a file,
   pipe or socket (section 3). On the real Lisa, port B needs a
   terminal or USB serial adapter with the right cable.
3. **ROM boot conventions.** The 0xAAAA tag check and the block 0 load
   address are known (section 3). Still to confirm: the ProFile read
   routine's register protocol (the notes come from the Priam booter),
   the MMU state at entry, and that the real ROM matches LisaEm's
   ROM-less boot path. Trace a UniPlus boot in LisaEm before writing
   `lisaboot.s`, and boot on the real Lisa in phase 1.
4. **LisaEm fidelity.** The UniPlus work found VIA timer and ProFile
   handshake bugs in LisaEm. Expect the same class of problem, and use
   the lisaem PR #55 build.
5. **Interrupt levels.** Minix's `lock()`/`unlock()` and `k_reenter`
   logic assumes the ST's MFP levels; map the Lisa's VIA and SCC
   levels onto it and check re-entrancy in the tick handler.
6. **Memory.** 1 MB is the floor and is ample for kernel + mm + fs +
   a few hundred KB of RAM disk + shell. Measure in phase 2 and size
   `NR_BUFS` and the RAM disk to fit 1 MB minus the video page. 2 MB
   in LisaEm needs a non-default build flag (section 3).
7. **UniSoft code.** Read for register sequences only. Every Lisa
   driver must be writable from the hardware manual alone; if a
   sequence exists only in UniSoft code, say so in the comment and
   re-derive it from the manual before shipping.
8. **Effort.** Phases 0–3 are the bulk: a few weeks of focused work
   each for phase 0 and phase 2, less for 1 and 3. Phase 5 is the
   interesting part and a week or two once the rest runs.

## 8. Repo layout

```
lisa-minix/
  PLAN.md            this file
  README.md          status, one paragraph, updated per phase
  LICENSE            Minix BSD 3-clause plus new-code notice
  PROVENANCE.md      origin and commit of every imported tree
  src/               Minix-ST 1.5 tree (kernel, mm, fs, lib, include,
                     commands, tools, test), Lisa files added under
                     src/kernel/lisa/ and src/lib/lisa/
  boot/              Lisa boot block
  tools/             Mac-side: mkfs, mklisa.py, mkimage, fsck
  docs/              memory-map.md, disk-layout.md, toolchain.md,
                     lisaem.md, hatari.md, esprofile.md
  CHANGELOG.md       per-change record with a Status line, as in the
                     UniPlus repo (period line not needed here beyond
                     the project-level PARTLY)
```

Disk images (`.image`) are never committed. Working images live in
`~/Documents/LisaEm Files`, and the LisaEm rules from the UniPlus repo
apply unchanged: quit LisaEm before touching an image, back up first,
verify with `pgrep -fl lisaem-arm64` and `lsof`.

## 9. Out of scope for this plan

Networking, X or any graphics beyond a text console, real-hardware
floppy installation, Minix 2.0 features, running UniPlus binaries,
512 KB Lisas, chain loading or booting through the ESProFile Selector
(the image must boot on its own), and multi-OS disk images.

## 10. Background

Why not Linux, in one paragraph: mainline Linux/m68k requires a 68020
or later with a paging MMU; the Lisa has a 68000 and a base-and-limit
segment MMU with no page tables, and the 68000 cannot restart a faulted
instruction, so demand paging, copy-on-write and mmap are impossible.
uClinux runs on a 68000 without an MMU, but only a 2.0/2.4-era kernel
fits in 2 MB, fork becomes vfork, there is no protection, and a 5 MHz
68000 would take minutes to boot. Minix 1.5 was designed for
MMU-less 68000s with 1 MB, and its 68000 port has drivers for bare
hardware, so it is the realistic second operating system for the Lisa.

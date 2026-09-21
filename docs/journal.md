# Project journal

A dated account of how the port went: decisions, problems, wrong turns and
how they were found. The other files in `docs/` describe the result; this
one keeps the story, for a later write-up. Times are local (CDT) on the
day, taken from commits.

## 14 September 2026

### Planning (17:30)

- `PLAN.md` written: Minix 1.5 rather than Linux (68000, no paging MMU,
  1-2 MB), starting from Minix-ST 1.5, the only 68000 port that ran on
  bare hardware.
- Review of the plan against local sources changed it:
  - LisaEm's serial port B, not A, can go to a host program.
  - Physical RAM on a Lisa does not start at 0 (1 MB LisaEm: 0x80000).
  - The target is real hardware through ESProFile from the start, so the
    disk image must be a raw ProFile image (532-byte blocks, 5 or 10 MB)
    that boots on its own: block 0 carries the boot ROM's 0xAAAA tag, no
    chain loader.
  - RAM floor set at 1 MB.

### Phase 0: the toolchain (17:37-19:04)

- Imported Minix-ST 1.5 unchanged; recorded provenance and the April 2000
  BSD licence.
- The tree was built with ACK's 16-bit `int` model. gcc's `-mshort`
  matches `int`, and the kernel, mm and fs compiled almost as-is.
- **First silent-corruption trap:** gcc `-mshort` keeps `size_t` and
  `ptrdiff_t` at 32 bits. Old-style calls with `sizeof` arguments push 4
  bytes where the Minix code reads 2, with no warning. Options were a
  patched gcc, 32-bit `int`, or ACK. Chose a patched gcc 16.2.0: two
  small changes (type sizes under `-mshort`, and the length argument of
  gcc's own `memcpy`/`memset` calls). Built reproducibly by
  `toolchain/build-gcc.sh`.
- ACK was not an ANSI compiler, so prototypes were off; gcc turned them
  on. `-D_MINIX_KR` keeps the old declarations.
- Tools written for the Mac: `elf2mnx.py` (ELF to Minix executables with
  GEMDOS relocation), `build.py` (port of `build.c`, which assumes 4-byte
  `long`), `mkfs.py`, and a mechanical ACK-to-GNU assembly converter.
- Reviewing the converted assembly found three real bugs: labels ACK
  exported implicitly, a branch that gas assembled longer and so shifted
  the boot sector's fixed fields, and `end` in the wrong section.
- Hatari boots it: first to "Insert ROOT diskette", then, after two more
  fixes, to a shell.
  - **Second silent-corruption trap:** the ST drivers use non-`volatile`
    register pointers; gcc merged two writes to the DMA mode register and
    floppy reads failed. ACK never optimized like that.
  - Hatari's floppy swap command was wrong at first (`hatari-path` has no
    floppy type), which made the first root-disk failure confusing.
  - I claimed in the docs that the test needed 2 MB, then ran it at 1 MB
    and corrected it.

### Phase 1: a kernel image on the Lisa (19:05-21:01)

- Wrote a position-independent boot block, the image builder `mklisa.py`
  (header in block 1), and a test kernel that prints boot parameters, the
  MMU segment registers, clock ticks and a bus error report on serial B.
- Found by reading LisaEm and UniPlus: in MMU setup mode each segment's
  first 0x4000 bytes decode as ROM space, so setup-mode code must sit at
  an address with bit 14 set.
- **Driving LisaEm without a person** took most of the phase. A branch
  of LisaEm (`minix-boot`) now:
  - logs message boxes and answers them (`LISAEM_NO_DIALOGS`), after
    `sample` showed the emulator stuck in a modal alert;
  - dumps the screen to PNG (`LISAEM_SCREEN_DUMP`), because window
    captures need a permission the terminal lacks. The first dump showed
    the ROM's STARTUP FROM menu; `-d` gets past it.
  - Also found: serial `File`/`Pipe` settings do nothing; the serial
    backend appears only when the guest first touches the SCC; a
    PseudoTTY mangled output, so tests use TelnetD. The emulator runs as
    a renamed copy so it cannot overwrite the user's preferences.
- **The long wrong turn:** the kernel read wrong bytes, crashed and
  restarted. The evidence pointed at LisaEm: the same code ran correctly
  under Hatari, the serial path was fine, and moving the stack sometimes
  helped. I documented it as a LisaEm fault. The user quit their own
  LisaEm and asked for CPU tracing. A trace added to the LisaEm branch
  (instructions, cached-decode checks, MMU state, a RAM watch and a RAM
  dump) showed a byte set to zero by the boot block's own copy loop, and
  then the last 20 bytes of every block zero. Cause: the boot block
  pointed the ROM's tag buffer 20 bytes below each data block, so every
  tag overwrote the end of the previous block. UniPlus's boot block
  avoids this by never moving its tag pointer. Fixed; the LisaEm notes
  were corrected.
- Also on the way: the COPS VIA's registers are 2 bytes apart, not 8; LisaEm
  raises level 2 for queued COPS bytes whatever the VIA says; a tick
  counter needed `volatile` (the third gcc trap of the same kind).
- Passed at 1 MB and 2 MB. The 2 MB LisaEm build started the new
  emulator under lldb with the user's own configuration (a `build.sh`
  behaviour); it held the user's UniPlus disk open about ten minutes and
  had to be killed. The image checked out afterwards, but a write could
  not be ruled out, and the user was told.

### Phase 2: Minix on the Lisa (21:01-21:17)

- `MACHINE == LISA`, with three new kernel files: vectors and context
  switch (from `stmpx.s`, with explicit trap numbers instead of the ST's
  trick of hiding them in the high byte of the vector address), `main`
  and interrupts (clock on vertical retrace), and a console on SCC B.
- The RAM disk root costs nothing to load: `build.py --lisa` appends the
  file system exactly where MM will allocate the RAM disk.
- Worked on the first boot to `login:`. The only test problem was LisaEm's
  deliberately slow serial input, solved by typing one character at a
  time.
- Found in passing: `rs_flush` in Minix-ST returns with interrupts locked
  when there is nothing to flush (fixed in the Lisa copy only).

### Phase 3: the ProFile (21:17-21:40)

- A polled ProFile driver from the protocol description, checked against
  UniPlus's standalone driver; `/dev/hd0` is the file system area named in
  the image header.
- `tools/minixfs.py` reads and checks Minix file systems on the Mac; it was
  tested against deliberately damaged images before being trusted.
- The two-boot test (write, `fsck`, reboot, read back, `fsck`, check on
  the Mac) passed. Typed input was lost while the disk was busy, so the
  test types more slowly.

### Phase 4: the Lisa's screen and keyboard (21:45-22:55)

- **Testing first.** Nobody is at the emulator, so the keyboard had to be
  typed by a script and the screen read by one. LisaEm already had an
  Edit/Paste path that types ASCII text through the COPS; the `minix-boot`
  branch now feeds it from a file (`LISAEM_KEYBOARD_FILE`), and
  `tools/lisascreen.py` turns the screen dump back into text by matching
  each character cell against the kernel's own font. The test can then
  check the screen exactly, not just look at it.
- Building LisaEm with `--allow2mbram` again, this time with
  `GDB=/usr/bin/true`, so `build.sh` had no debugger to launch. Nothing
  started.
- **Screen:** `lisavdu.c` from Minix-ST's `stvdu.c`, minus colour and sound.
  UniPlus showed where the screen goes (top 32 KB of RAM, latch = physical
  address / 32 KB) and the text size (90 x 40). The ST's 8 x 8 font plus a
  blank line gives exactly 90 x 40 on 720 x 364. A set bit is black on the
  Lisa, so the font copies straight in as black on white. The cursor code
  now remembers whether the cursor is shown; the ST relied on strictly
  alternating calls.
- **Keyboard:** `lisakbd.c`, a small state machine over the COPS byte
  stream (keys, reset codes, mouse and clock reports) with US key tables
  written from UniPlus's key code facts. Its interrupt enable had been off
  since phase 1 (LisaEm raises level 2 anyway); it is now on for CA1 only,
  which a real Lisa needs.
- **Console selection:** the plan said to choose serial or screen at boot.
  Mirroring both ways made the choice unnecessary and kept every earlier
  test working unchanged.
- It worked on the first boot: login and `ls -l /etc` typed on the Lisa
  keyboard, output on the Lisa screen.
- The symbol test found two things that were not driver bugs. `@` erased
  the line: it is Minix 1.5's kill character. `|` came out as `?`: LisaEm
  types `|` on the `/ ?` key, although its own `\` is on the right key and
  UniPlus agrees that `|` is Shift-`\`. Documented as a LisaEm bug for
  later.
- **A hang, then a crash, both at the edge of the emulator.**
  - In one of the first runs, output stopped in the middle of a scrolling
    `ls /bin` and never resumed. Five more runs passed. Reading LisaEm's
    COPS code suggested the mouse: once the pointer has moved over the
    window, LisaEm's COPS VIA always reports data, and reading it makes up
    another mouse report, so a handler that reads "while there is data"
    never finishes. A new LisaEm test hook (`LISAEM_MOUSE_MOVE_AT`) moved
    the mouse once, and the old handler froze on the first keystroke:
    reproduced.
  - The fix (one byte per interrupt, as UniPlus does) crashed on the first
    key with a bus error panic. A CPU trace from the first keystroke (a
    trace start trigger was added to LisaEm for this) showed the level 2
    handler starting 8 bytes past its entry, after an interrupt taken
    during the clock handler's VRT_ON write. LisaEm fires pending VIA
    interrupts from inside that write, and the write instruction then adds
    its own length to the handler's address. A LisaEm bug; Minix now masks
    interrupts around the two writes, and LisaEm delivers the interrupt
    afterwards.
  - Both are documented for LisaEm, and the console test now moves the
    mouse every run.
- Passed at 1 MB and 2 MB (`make lisaem-console`); phase 2 and 3 tests and
  the Atari build still pass.

## 15 September 2026

### Phase 5: the MMU (until 02:45)

- **Design.** Minix-ST had no MMU and made fork work by shadowing. UniPlus
  showed how the Lisa MMU is driven (register layout, one user context
  reloaded per process, register writes going to the SEG-latched context
  even in supervisor mode); LisaEm's source confirmed that setup-mode RAM
  access still goes through context 0, so the kernel can rewrite context 1
  while running normally.
  - Kernel and tasks stay in context 0 exactly as the boot ROM left it, so
    every "physical" address in Minix stays valid and the kernel still
    reaches all memory directly.
  - Everything in user mode runs in context 1, loaded from the process's
    memory map in `restart`. Exec'd programs get virtual address 0, which
    also removes relocation; fork becomes a copy.
  - The whole change to shared code came down to a handful of places that
    assumed virtual = physical: `umap`, message copying, fork, exec's
    address choice, and one stack check. An audit of every kernel and MM
    path that touches user memory found them before any code was written.
  - Rejected: read-only text (text and data share segments in the Minix
    memory map), a context per process (only three), and running MM and FS
    in supervisor mode (unpreemptible, and their stacks would take
    interrupts).
  - Found by planning, not debugging: the MMU counts 512-byte pages and
    Minix 256-byte clicks, so MM now allocates even clicks and the boot
    image is padded to 512 bytes; otherwise INIT's first fork would have
    landed half a page off.
- **Measuring first.** A phase 4 kernel built in a separate git worktree,
  with the same new test programs, gave the "before" numbers. Getting them
  took three tries, none of them the kernel's fault:
  - `times()` returns no elapsed time in Minix 1.5; the benchmark uses
    `time()` and the serial log's timestamps.
  - Every child in the benchmark's "write 32 KB" loop died with SIGSEGV:
    with 16-bit `int`, `j < 32768` can only fail by overflow, so gcc made
    the loop endless and it ran off the end of RAM. My bug.
  - `test1` hung: it waits for a signal handler with `while (glov == 0);`,
    which gcc also made endless. The tests are now built `-O0`. Same trap
    as the phase 1 `volatile` tick counter.
  - A long command typed over serial lost characters; the tests now run
    from a script on the disk image.
- **First boot of the MMU kernel worked**: login, pipes, and `segv` killed
  with "Memory fault - core dumped" while the system carried on.
- **A LisaEm crash.** The full run stopped at `test18` with no output. The
  runner said "did not match" because LisaEm had died; macOS crash reports
  showed a SIGSEGV in LisaEm's instruction pre-decoder. A `-g` unstripped
  build gave the line: a special case reads past its array when an
  instruction's operand equals the address of the end of its code page.
  In page 0 that address is 0x1FF, the Unix mode mask 0777, which
  `test18` uses. No guest had run code in page 0 before; running programs
  at virtual address 0 exposed it. Fixed on the LisaEm branch.
- **Results** (1 MB and 2 MB): tests 0-19 pass as before; 20 and 21 fail
  exactly as they did with shadowing. The test suite takes 460 s instead of
  1008 s; fork-heavy tests are up to 12 times faster, others up to 2.5 times
  slower. A theory that IDLE's MMU reloads cost the time was tested (IDLE in
  supervisor mode), made no difference, and was reverted; the likelier
  cost is MM and FS being user processes, two reloads per system call.
- Also added: control-T prints the kernel's process table dump, from the
  Lisa keyboard or the serial port.

### Phase 6: the real-time clock (02:50-03:40)

- The keyboard driver already skipped the COPS's clock report; it now
  decodes it. Sending the read-clock command follows UniPlus's COPS
  handshake (command in port A, wait for CRDY low, drive port A briefly).
- First boot: `date` showed the Mac's time of day to the second, on
  "Mon Sep 14 1987". The year is the COPS's 1980-1995 nibble, which
  LisaEm sets to 7; the day was one early, which traced to LisaEm counting
  days from 0 where UniPlus counts from 1.

### Phase 6: the multi-file commands (03:45-04:20)

- Eighteen programs from `src/commands/*/` built almost unchanged; two
  compile errors in elvis.
- Before trusting them: `-w` hides a 16-bit trap, undeclared functions
  returning pointers. An audit of all commands, library, MM, FS and kernel
  found five real cases (`cron`, `unshar`, `indent`, `nroff`) that were
  already on the disk image.
- Testing the editors from the keyboard found a keymap conflict: LisaEm
  types the host's Escape as the Lisa's Clear key, and Clear was DEL, the
  interrupt character, as in UniPlus. Clear is now ESC.
- Smaller things: elvis needs `/usr/tmp`; mined's default build is fixed
  80 x 25 and its termcap build needs `-DUNIX`; Minix's kermit has its help
  text compiled out; `|` typed in LisaEm is still `?`, which broke the
  test's own end marker.

### Phase 5 follow-up: three user contexts (04:20-04:45)

- The phase 5 slowdown on ordinary system calls pointed at MM and FS being
  user processes, so every call reloaded context 1 twice. The Lisa MMU has
  three user contexts: MM now has context 2 and FS context 3, loaded once,
  and a call to them costs two latch writes each way. The test suite went
  from 460 s to 356 s; the tests that had become slower than shadowing are
  now within a few seconds of it.

### Phase 6: keyboard auto-repeat and a real-hardware guide (04:45-05:05)

- `docs/esprofile.md`: what to copy to the ESProFile card, the boot output
  to expect, tests in order, and the ten assumptions checked only in
  LisaEm, for the first boot on a real Lisa.
- Auto-repeat, driven by the clock tick as on the ST. Testing it needed a
  held key, which LisaEm's paste path cannot do; the keyboard file now
  takes raw COPS codes. The first test attempt pressed and released the key
  in the same instant because LisaEm reads new keyboard input only after
  the previous paste has finished; the test now waits for the echo.

### Phase 6: the Sony floppy (05:05-05:45)

- A research subagent summarised the controller protocol from UniPlus and
  LisaEm; LisaEm needed one more hook, inserting a disk while the guest
  runs (`LISAEM_FLOPPY_AT`), and a Mac tool to make DC42 images.
- First runs: a clean "no disk" (the test typed `mount` before the disk
  went in), then `mount` refusing a disk that `dd` copied byte for byte.
  Printing the superblock FS saw showed 68000 code, and printing the buffer
  right after the driver's copy showed the copy had not happened: the task
  had waited for its interrupt in the same global message buffer that held
  the request. `dd` had worked only because FS's read-ahead passes the
  driver a copy of the request.
- The test now copies 170 KB of programs to the floppy and back to the Mac.

## 21 September 2026

### LisaEm changes upstream

- The LisaEm work lived on a local branch, `minix-boot`, so nobody else
  could run the emulator tests. The fixes had mostly gone upstream one by
  one already (#66, #67, #68, #72); the rest went as two pull requests,
  rebased onto current `master` and written without reference to Minix:
  the automation and trace environment variables with a short
  `EnvironmentVariables.md` (#74), and the COPS clock's day of the year
  (#75). Both merged. The tests now need only upstream LisaEm.
- The clock had been left as a note ("to be checked"). UniPlus `rtc.c`
  counts days 1-366, and LisaEm's own clock tick already went from day 365
  to day 1, never 0; only the start-up value from the host's `tm_yday` was
  0-based. On 21 September the old build gave Minix `Sun Sep 20`, the
  fixed one `Mon Sep 21`.
- The phase 4 console test's final screen check failed (`# # sync`) with
  the old `minix-boot` build as well as the new one, so it was not from the
  LisaEm changes. The test typed Command-D and then Return: when the shell's
  prompt came later than the Return's echo, the shell read an empty line
  and prompted twice. The test now sends Command-D alone.
- LisaEm rebuilt from upstream `master` (`3020b09`, `--allow2mbram`); the
  whole suite passes on freshly built images: `lisaem-test`,
  `lisaem-minix`, `lisaem-hd`, `lisaem-console` (1 MB and 2 MB),
  `lisaem-cmds`, `lisaem-floppy` and `tests/lisaem-mmu.sh` (test20 and
  test21 fail as they did before phase 5).

## Recurring lessons

- gcc's optimizer is the main hazard in 1990 68000 code: anything
  hardware or interrupts change must be `volatile`, and `-mshort` alone
  does not reproduce a 16-bit-`int` compiler.
- "Works under Hatari, fails in LisaEm" did not mean LisaEm was wrong. A
  trace settled it within two runs; guessing had not in two hours.
- Measure before changing: the shadowing baseline turned up three test
  and benchmark bugs that would otherwise have been blamed on the MMU.
- A new, legitimate guest behaviour (code at address 0) can find an old
  emulator bug. A symbolicated crash report settled it in minutes.
- An emulator is not the hardware: both of phase 4's crashes were in
  how the kernel met LisaEm's shortcuts (COPS reads, interrupt timing),
  not in its logic. Reproducing them on purpose (a mouse hook, a trace
  start trigger) turned "rare hang" into a fix and a bug report.
- Automating an emulator built for a person needs its own tooling (dialogs,
  screen, serial, preferences isolation), and its build script can do
  surprising things.

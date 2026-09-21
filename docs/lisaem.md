# LisaEm

How the Lisa side of this project is tested without anyone at the
emulator, what was learned doing it, and what is still broken.

Status: 14 September 2026. The phase 1 test kernel passes in LisaEm at
1 MB (three runs out of three) and at 2 MB (`--mem 2048`): boot from a
`mklisa.py` image, banner and context 0 MMU segments on serial port B, ten
vertical retrace ticks, and a correct bus error report. Not yet run on a
real Lisa.

## LisaEm build

Upstream LisaEm, `master` at `3020b09` or later, in `~/github/lisaem-minix`:

```
git clone https://github.com/arcanebyte/lisaem ~/github/lisaem-minix
```

The changes this project needed are all upstream: the environment
variables below (#74), the COPS clock's day of the year (#75), and fixes
for a pre-decoder crash (#72), an interrupt taken in the middle of an
instruction (#66), the video latch (#67) and the floppy format byte (#68).
Until September 2026 they were on a local branch, `minix-boot`, whose
`MinixBootNotes.md` is cited below.

LisaEm needs wxWidgets 3.2, built as LisaEm's own build instructions
describe; the `PATH` entry below is where that build installed it on this
machine. Built with

```
cd ~/github/lisaem-minix
PATH="/usr/local/wx3.2.11-cocoa-macOS-26.5-x86_64,arm64/bin:$PATH" ./build.sh build
```

`tools/lisaem_run.py` runs `~/github/lisaem-minix/bin/LisaEm.app` with the
boot ROM `/Applications/LisaEm Files/H ROM/boot.ROM`; `--lisaem` and
`--rom` choose others. The ROM is not part of either repository.

The current binary was built with `--allow2mbram`, needed for
`--mem 2048`. **Warning:** any `build.sh` option that adds a compiler
define (`--allow2mbram`, `--debug`, ...) makes the script start the new
LisaEm under lldb with `run -p` and the *default* configuration, which
powers on with the user's own disk images. Build with `GDB` set to a
program that is not a debugger, which makes the script launch nothing:

```
GDB=/usr/bin/true PATH="/usr/local/wx3.2.11-cocoa-macOS-26.5-x86_64,arm64/bin:$PATH" \
    ./build.sh build --allow2mbram
```

The environment variables the tests use, described in LisaEm's
`EnvironmentVariables.md`:

- `LISAEM_NO_DIALOGS`: message boxes are written to stderr and answered
  with their default button instead of waiting for a click.
- `LISAEM_SCREEN_DUMP=<file.png>`: the Lisa display is saved to the file
  about once a second.
- `LISAEM_KEYBOARD_FILE=<file>`: bytes appended to the file are typed on
  the Lisa keyboard through LisaEm's paste-to-keyboard path (newline is
  Return; ^A and a byte is that raw COPS key code, for holding a key
  down). `tools/lisaem_run.py --keyboard` uses it for its `--send` steps; a
  step's text ending in `\c` is sent without Return.
- `LISAEM_FLOPPY_AT=<seconds>,<image>`: once, insert a DC42 floppy image
  into the drive, without restarting from it as `-f` does.
- `LISAEM_MOUSE_MOVE_AT=<seconds>`: once, that many seconds after start,
  do what moving the pointer onto the Lisa screen does.
- `LISAEM_CPU_TRACE=<file>` with `_RANGE`, `_MAX`, `_MODE`, `_START`,
  `_WATCH` and `_DUMP`: an instruction trace (from the first time the PC
  reaches `_START`, if given) with registers, checks of LisaEm's
  instruction cache and MMU state, a watch on one RAM byte, and a RAM dump
  at a given PC. After changing it, rebuild with `./build.sh clean` first.

Pass them through the runner with `--env`, for example

```
python3 tools/lisaem_run.py build/lisa/lisatest.image --until halted \
    --env LISAEM_CPU_TRACE=$PWD/build/lisaem/cpu.trace \
    --env LISAEM_CPU_TRACE_RANGE=0-3fff
```

## Running a test

```
make lisaem-test
```

builds `build/lisa/lisatest.image` and runs `tools/lisaem_run.py` on it:

1. writes `build/lisaem/lisaem-minix.conf`: boot ROM H, I/O ROM A8, 1 MB,
   the image as the parallel-port ProFile, serial B on TelnetD at a free
   local port, no slot cards, and the parameter RAM from
   `tools/lisaem-pram.txt`;
2. starts `LisaEm.app/Contents/MacOS/lisaem-minix` (a copy of the
   executable) with `-c <config> -p -d` and both environment variables;
3. connects to the serial port as soon as LisaEm listens, and logs every
   line with its elapsed time to `build/lisaem/serial.log`;
4. stops when the output matches `--until` or at `--timeout`, kills that
   LisaEm, and checks that the user's own LisaEm preferences are unchanged.

The screen is in `build/lisaem/screen.png`, LisaEm's own output in
`build/lisaem/lisaem.log`. `tools/lisascreen.py build/lisaem/screen.png`
prints the text Minix has drawn on it (phase 4 and later), by matching
every character cell against the kernel's font.

### Why it is done this way

| Problem | Evidence | Handling |
|---|---|---|
| A second LisaEm instance would overwrite the user's preferences | The wx preferences file is named after the executable (`~/Library/Preferences/lisaem-arm64-26.05-wx3 Preferences.2`) and stores `lisaconfigfile=` | Run a copy named `lisaem-minix`, which gets `lisaem-minix Preferences`; hash the user's files before and after |
| Modal dialogs stop the emulator with nobody to click them | `sample` showed the main thread in `-[NSAlert runModal]` | `LISAEM_NO_DIALOGS` |
| No way to see the screen: `screencapture -l` fails from this terminal, and scripting the window through System Events hangs on a permission prompt | | `LISAEM_SCREEN_DUMP` |
| The ROM shows STARTUP FROM instead of booting, with and without a PRAM copied from a working configuration | screen dump | `-d`, which presses Apple-3 in the first seconds |
| Serial `File` and `Pipe` do nothing | `z8530.c` has no case for them | TelnetD |
| The serial backend only appears when the guest first touches the SCC from RAM, and TelnetD drops output until a client connects | `avoid_rom_scc_tests()` in `z8530.c` | The test kernel waits a moment after setting up the SCC; the script polls for the listener every 10 ms |
| PseudoTTY output arrived with doubled newlines, and changing the terminal settings lost characters | `serial.log` | TelnetD, read as raw TCP; the script drops telnet command bytes (0xFF and the two after it) |

## Results

### Boot (works)

- `tools/mklisa.py` images (block 0 tag `AAAA`, header in block 1) boot:
  the ROM loads `boot/lisaboot.S`, which reads the header and 33 image
  blocks through the ROM read routine at 0xFE0090, copies the image to
  logical 0 and jumps to it. The kernel prints on SCC port B.
- Boot device byte (0x1B3): 02. Physical start of RAM (0x2A4): 0x80000.
- Context 0 as the ROM leaves it, read in setup mode, 1 MB configuration:

  | Segment | Logical | Limit | Origin | Physical |
  |---|---|---|---|---|
  | 00-07 | 000000-0FFFFF | 700 (read-write, full) | 400-B00 pages | 080000-17FFFF |
  | 7E | FC0000 | 900 (I/O) | 000 | |
  | 7F | FE0000 | F00 (special I/O) | 000 | |
  | 118 others | | C00 (invalid) | | |

  See `docs/memory-map.md`.

### Clock, COPS and faults (works)

Serial log of a passing run (`make lisaem-test`, 14 September 2026):

```
   8.41 Minix/Lisa phase 1 test kernel
   8.41 boot device 02  physical RAM base 00080000  logical end of RAM 00100000 (1024 KB)
   ...
   8.44 counting vertical retrace interrupts
   9.53 tick 60  COPS interrupts 0
  ...
  19.43 tick 600  COPS interrupts 0
  19.44 reading unmapped address 800000 to test the bus error handler
  19.44 bus error: access address 00800000 function code 0015 (read) instruction 2F79 sr 2004 pc 00001A18
  ...
  19.45 halted
```

Ten ticks of 60 take about 10 s of host time, so LisaEm's vertical retrace
runs at about 60 Hz here.

Lessons from getting there, all now in the code:

- **Boot block tags.** The ROM read routine writes each block's 20 tag
  bytes where `a1` points. Pointing it 20 bytes below each block's data
  buffer made every tag overwrite the last 20 bytes of the block before.
  The symptoms (wrong bytes, jumps to 0, restarts, varying with code
  layout) looked like an emulator fault, and were first documented as
  one; the CPU trace found the real cause. All tags now go to one buffer.
- The COPS VIA's registers are 2 bytes apart, the parallel VIA's 8
  (UniPlus `cops.h`, `pport.h`); masking the COPS VIA at the wrong address
  left level 2 interrupts on.
- LisaEm raises level 2 while the COPS has queued keyboard events (`-d`
  queues Apple-3), whatever the VIA interrupt enable register says
  (`irq.c`); the kernel needs a level 2 handler that reads the COPS data.
- Variables changed by interrupt handlers must be `volatile`, or gcc reads
  them once and a wait loop never ends.

### Screen and keyboard (phase 4, works)

`make lisaem-console` types on the Lisa keyboard through
`LISAEM_KEYBOARD_FILE` and reads the Lisa screen back with
`tools/lisascreen.py`; results in `docs/lisa-kernel.md`. Found on the way:

- **`|` arrives as `?`.** LisaEm's host-to-Lisa key table types `|` as
  Shift and key code 0x4C (the `/ ?` key), while `\` is key code 0x42; on
  the Lisa, and in UniPlus's tables, `|` is Shift-`\`. A LisaEm bug, noted
  in the branch's `MinixBootNotes.md` to be fixed later; the test leaves
  `|` out. (Typing `|` on the Mac keyboard in the LisaEm window may use a
  different path; not checked.)
- `@` typed in a shell line erases the line: it is Minix 1.5's line-kill
  character in cooked mode, not a keyboard problem.
- **A hang caused by the mouse.** In one of the first runs the system
  stopped in the middle of `ls /bin` output (screen and serial stopped
  after the same character) and never recovered. The keyboard handler then
  read the COPS while the VIA's interrupt flag register showed CA1. In
  LisaEm, reading that register sets CA1 whenever the key queue is empty
  and a mouse event is queued (`via6522.c`, `IFR1`), and reading port A
  with an empty key queue returns a fresh mouse report `00 dx dy`
  (`cops.c`, `via1_ira`). The boot ROM leaves the mouse on
  (`plugmouse`), and the mouse queue is emptied only by
  `seek_mouse_event`, which follows the Lisa Office System's mouse
  globals, so once the pointer has moved over the window the loop never
  ends, with interrupts locked. **Reproduced** with the branch's
  `LISAEM_MOUSE_MOVE_AT=10` (one pointer movement ten seconds in): the old
  handler froze on the first typed character. The handler now reads one
  byte per interrupt, as UniPlus does, and the console test moves the
  mouse every time. For LisaEm the IFR behaviour is arguably a bug too: a
  real 6522 sets CA1 on the COPS's strobe, not when the register is read.
- **LisaEm takes an interrupt in the middle of an instruction.** With the
  one-byte handler the kernel crashed on the first key (`sig=4 ... pc=604`,
  then a bus error panic). A CPU trace started at the first keystroke
  (`LISAEM_CPU_TRACE_START`, added to the branch for this) showed a level 2
  interrupt arriving while the level 1 (clock) handler wrote VRT_ON. The
  exception frame held the PC of that write instruction itself, and the
  level 2 handler began 8 bytes past its entry, skipping the two
  instructions that save registers and push the vector number, so its
  return popped garbage and jumped to 0. Cause in LisaEm: the VRT_ON write
  calls `reset_video_timing()`, which calls `get_next_timer_event()`, which
  (`irq.c`, "2021.03.21 fire interrupt if IFR set to enabled bits") calls
  `reg68k_external_autovector` for any VIA with an enabled flag set, from
  inside the instruction; the instruction then adds its length (8 bytes)
  to the PC, which is by now the handler's address. VIA shift register
  writes reach the same code. Minix now locks interrupts around the VRT
  writes, so LisaEm keeps the interrupt pending until the mask drops
  (`lisa_level1` in `lisamain.c`). The first, looping handler was exposed
  too, only less often, because it rarely left CA1 pending. A LisaEm bug,
  noted in `MinixBootNotes.md` to be fixed later.

### MMU (phase 5)

`tests/lisaem-mmu.sh`; results in `docs/memory-map.md` and
`docs/lisa-kernel.md`. Found on the way:

- **LisaEm crashed in `cpu68k_makeipclist`** (host SIGSEGV) as soon as
  Minix `test18` ran. Programs now run at virtual address 0, so their code
  is in logical page 0, and a special case in the instruction pre-decoder
  compares an operand with the page's end address (0x1FF there, the mode
  mask 0777) and then reads past its list of decoded instructions. Fixed
  upstream (#72, first on the branch as `b7347be`). Found by building
  LisaEm with `CFLAGS=-g ./build.sh build --no-strip` (and `GDB=/usr/bin/true`)
  and running `atos` on the macOS crash report in
  `~/Library/Logs/DiagnosticReports`.
- The runner reported a crashed LisaEm as "did not match ... within N s";
  a LisaEm that exits is logged as "LisaEm exited" first, and that line is
  the thing to look for.

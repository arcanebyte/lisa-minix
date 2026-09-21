# Porting Minix 1.5 to the Apple Lisa

This is a technical account of the port: what it needed, what had to be
found out, and what went wrong. It is written for an engineer who might do
the same thing, possibly with a different compiler, emulator or disk
emulator. It is organised by subject, not by date. The dated record, with
wrong turns in order, is `docs/journal.md`; the per-subject reference
documents are in `docs/`.

## Status

Minix 1.5 boots on a Lisa 2 in LisaEm, at 1 MB and 2 MB, from a raw ProFile
image that the boot ROM starts directly. It runs on the Lisa's screen and
keyboard and on serial port B at the same time, with its root file system
on the ProFile, a Sony 400K floppy driver, the COPS real-time clock, and
user processes under the Lisa MMU. All the single-file commands and the
larger ones (elvis, mined, make, kermit, nroff and others) are built and
run. Minix system call tests 0 to 19 pass; 20 and 21 fail the same way on
the Lisa as on an unmodified kernel using shadowing, so they are not MMU
failures.

**Nothing has been run on a real Lisa.** Every hardware statement below
was checked in LisaEm, against the UniPlus sources, or both.
`docs/esprofile.md` lists the eleven places where a real machine could
differ and what each failure would look like.

The Lisa work was done on 14 and 15 September 2026; the emulator changes it
needed went upstream on 21 September. New code: about 2,800 lines of kernel
C and assembly in `src/kernel/lisa/`, a 130-line boot block, and about
2,100 lines of Python host tools.

## What you need

### Sources

| What | Why |
|---|---|
| Minix-ST 1.5 (github.com/EmmanuelKasper/minix-st-1.5) | The base tree: a 68000 Minix that runs on bare hardware with its own drivers. MacMinix ran under Mac OS, so its drivers are no use. BSD licence since April 2000. |
| UniPlus V.1.5+ sources for the Lisa | A working Unix with drivers for every Lisa device. Read for register sequences and protocols, never copied: the code is UniSoft's. Every Lisa driver here says in comments which fact came from which file. |
| LisaEm source | The emulator, and a second description of the hardware. Where UniPlus says what to write to a register, LisaEm's source says what the emulated device does with it. |
| Lisa boot ROM H image | Needed to run LisaEm the way a real Lisa boots. Not redistributable, not in this repo. |
| ESProFile source | Defines the disk image format a real Lisa will be given: file name, block layout, the two accepted sizes. |
| Minix 1.5 reference manual (Prentice Hall, 1990) | Matches this source tree. |
| Lisa Hardware Manual (Bitsavers) | The intended authority. In practice UniPlus and LisaEm answered most questions faster, and several facts here still need checking against it. |

### Tools

| Tool | Version used |
|---|---|
| Host | macOS 26, arm64 |
| gcc | 16.2.0, target m68k-elf, patched (below), built by `toolchain/build-gcc.sh` |
| binutils | m68k-elf-binutils 2.47 from Homebrew |
| Python | 3.14, standard library only |
| Hatari 2.6.1 with EmuTOS 1.4 | Atari ST emulator, for the reference build |
| LisaEm | upstream `3020b09` or later |

### Why Minix, and why this Minix

Linux/m68k needs a 68020 and a paging MMU. The Lisa has a 68000, which
cannot restart a faulted instruction, and a base-and-limit segment MMU, so
demand paging and copy-on-write are impossible. uClinux gives up fork and
protection. Minix 1.5 was written for a 68000 with 1 MB and no MMU, the
whole system takes about 140 KB of memory, and its drivers are tasks that
can poll their devices, which suits the ProFile.

## The compiler

This was the part with the most ways to go silently wrong, and the part
most likely to differ if you use other tools, so it gets the most space.
Reference: `docs/toolchain.md`.

### Find the integer model first

Minix-ST was built with the Amsterdam Compiler Kit (ACK), which existed
for the 68000 with 16-bit and with 32-bit `int`. Nothing in the tree says
which in words. The evidence:

- `include/limits.h` has `INT_MAX 32767`.
- `lib/atari/sendrec.s` reads `send(dest, ptr)`'s first argument as a word
  at `4(sp)` and the pointer as a long at `6(sp)`.
- `lib/atari/crtso.s` pushes `argc` with `move.w`.
- `include/sys/types.h` has `typedef unsigned int size_t`.

So `int` is 16 bits, `long` and pointers 32. gcc's `-mshort` gives that.
Moving the tree to 32-bit `int` instead would have meant changing every
assembly routine that takes C arguments, the headers and the pointer-walk
`va_arg`, and then auditing the C for code that relies on the width. It
would also make code larger and slower on a 68000. Building ACK was the
fallback and was not needed.

### What stock `-mshort` gets wrong

With `-mshort`, gcc makes `int` 16 bits and the stack slot for an argument
16 bits (`PARM_BOUNDARY`), but the m68k-elf target still defines

```
#define PTRDIFF_TYPE "long int"
#define SIZE_TYPE "long unsigned int"
```

so the result of `sizeof` and of a pointer subtraction is 32 bits. The
Minix headers say `size_t` is `unsigned int`, so the library is compiled
to take 16-bit lengths. The two disagree, and the tree is K&R C with no
prototypes in scope to convert the argument. Take

```
write(fd, (char *) &utmp, sizeof(struct utmp));
```

from `tools/init.c`. Stock gcc pushes the length as 4 bytes. `write` reads
2. The 68000 is big-endian, so it reads the high half, which is zero for
any object under 64 KB, and any argument after it is found 2 bytes out of
place. There is no diagnostic, because an unprototyped call is allowed to
pass anything. Of the 83 `read` and `write` calls in `commands/` that pass
a `sizeof`, 82 have no cast. ACK's `sizeof` was 16 bits, so the code was
correct when it was written.

Turning the prototypes on is not a way out. gcc defines `__STDC__`, which
made the tree's `_PROTOTYPE` macro produce ANSI prototypes, and those
conflict with the K&R definitions in the library, where about a dozen
files then failed to compile. The build defines `-D_MINIX_KR` to keep the
old-style declarations, which is how ACK saw the code.

There is a second, less visible case. When gcc copies or clears a struct
too big to do inline, it emits its own call to `memcpy` or `memset`, and
passes the length in its internal type for sizes, `sizetype`. The callee
is Minix's library `memcpy`, which takes a 16-bit length. No source line
shows this call.

### The patch

`toolchain/gcc-16.2.0-mshort-size_t.patch`, 49 lines, two files.

**1. `gcc/config/m68k/m68kemb.h`** (included by the m68k-elf and
m68k-rtems targets):

```
#define PTRDIFF_TYPE (TARGET_SHORT ? "int" : "long int")
#define SIZE_TYPE (TARGET_SHORT ? "unsigned int" : "long unsigned int")
#define SIZETYPE "long unsigned int"
```

`SIZE_TYPE` and `PTRDIFF_TYPE` are the C-level types: what `sizeof`
yields, what `size_t` and `ptrdiff_t` mean in gcc's own headers and
builtins. They now follow `int` under `-mshort` and are unchanged without
it.

The third line matters as much as the first two. `SIZETYPE` is gcc's
internal type for object sizes and offsets, used in address arithmetic
such as array indexing. `gcc/defaults.h` sets it to `SIZE_TYPE` unless the
target says otherwise, so changing `SIZE_TYPE` alone would also have made
offset arithmetic 16 bits on a machine with 32-bit pointers. Defining
`SIZETYPE` explicitly keeps it at 32.

**2. `gcc/expr.cc`**, in `emit_block_op_via_libcall` (the `memcpy`,
`memmove` and `memcmp` calls gcc generates) and `set_storage_via_libcall`
(`memset`):

```
-  size_mode = TYPE_MODE (sizetype);
+  size_mode = TYPE_MODE (size_type_node);
   ...
-  size_tree = make_tree (sizetype, size);
+  size_tree = make_tree (size_type_node, size);
```

After change 1, `sizetype` (32 bits) and `size_t` (16 bits) differ, which
is true of no stock gcc configuration, and these two functions were
passing the length as `sizetype` to functions whose parameter is `size_t`.
Now they pass `size_t`. On every target where the two types have the same
mode this generates identical code.

Generated code, before and after:

| Source | Stock `-mshort` | Patched `-mshort` |
|---|---|---|
| `kr(3, buf, sizeof(buf), 5)` | `pea 10` (4 bytes) | `movew #10,sp@-` |
| `kr(a - b)`, two `char *` | `movel d0,sp@-` | `movew d0,sp@-` |
| `x = y`, a 200-byte struct | `memcpy` with `pea 200` | `memcpy` with `movew #200,sp@-` |

The way to check a patch like this is that table: compile three-line test
functions with `-S` and read the pushes. Nothing at run time points at the
cause.

The limit that results is the one ACK had: one object, one struct copy or
one pointer difference cannot exceed 65,535 bytes.

### Building it

`toolchain/build-gcc.sh` downloads gcc 16.2.0, checks its SHA-256, applies
the patch and configures with

```
--target=m68k-elf --program-prefix=m68k-minix-
--with-cpu=68000 --disable-multilib
--enable-languages=c --without-headers --with-newlib
CFLAGS_FOR_TARGET="-O2 -mcpu=68000 -mshort"
```

then builds only `all-gcc` and `all-target-libgcc`. The points that matter:

- libgcc is compiled with `-mshort`. Its routines take `int` arguments
  (shift counts, for one) and must agree with their callers about the
  width. `--disable-multilib` with `--with-cpu=68000` means there is one
  libgcc and it contains no 68020 instructions.
- No C library is built. Minix has its own, and the build uses `-nostdinc`.
- The programs install as `m68k-minix-*` so they cannot be mistaken for
  Homebrew's unpatched `m68k-elf-gcc`.
- Assembler and linker are Homebrew's m68k-elf binutils, unpatched.

If you use another gcc version, the first hunk should carry over with
little change. For the second, look for calls that build a libcall's
length argument from `sizetype`; the functions have moved and been renamed
over the years.

### Flags

```
m68k-minix-gcc -mcpu=68000 -mshort -O -std=gnu89 -D_MINIX_KR \
    -fno-builtin -fno-tree-loop-distribute-patterns \
    -nostdinc -Isrc/include
```

- `-fno-builtin`: without it gcc replaces calls to the library's own
  `memcpy`, `strcpy` and the rest with builtins whose prototypes disagree
  with the K&R definitions (683 `-Wbuiltin-declaration-mismatch`
  warnings).
- `-fno-tree-loop-distribute-patterns`: without it gcc recognises the loop
  inside the library's `memset` as a `memset` and compiles it to a call to
  itself.

With these, plus forward declarations in five files where a `static`
function was used before its definition, every file in `kernel`, `mm`,
`fs` and `lib` compiled. Source changes made for the compiler, the
`volatile` fix below included, came to about thirty lines.

### What the optimizer breaks

ACK did almost no optimization. gcc does, and code from 1990 assumes it
won't. None of these produces a warning:

| Code | What gcc did | Seen as |
|---|---|---|
| ST drivers write device registers through non-`volatile` pointers | merged two writes to the DMA mode register into one | floppy reads failing with `dma status = 0x1` under Hatari |
| tick counter set in an interrupt handler, not `volatile` | read it once; the wait loop never ended | phase 1 test kernel hanging |
| `while (glov == 0);` in `test1.c`, waiting for a signal handler | the same | `test1` hanging |
| `for (j = 0; j < 32768; j++)` with 16-bit `int` | the loop can only end by signed overflow, which is undefined, so it never ends | every benchmark child dying of SIGSEGV after running off the end of RAM |
| `while (--delay >= 0);` delay loops | removed | not seen in emulators; would matter on real ST hardware |

The rules that came out of it: all device access through `volatile`
pointers; anything an interrupt or signal handler changes is `volatile`;
no timing by empty loops; loop counters that reach 32768 are `unsigned` or
`long`. The Minix test programs are built `-O0`, because they are tests of
the system, not of how well their authors anticipated gcc.

One 16-bit trap is not the optimizer's. A call to an undeclared function
is assumed to return `int`, so an undeclared `malloc` returns the low 16
bits of a pointer. The build uses `-w`, because code of this age warns
constantly under a modern gcc, and that hides this one. An audit with
`-Wint-conversion -Wint-to-pointer-cast` over everything found five real
cases (in `cron`, `unshar`, `indent` and `nroff`). They were bugs under
ACK too.

Checked and fine: gcc returns pointers in `d0` as ACK did; gcc pushes all
arguments on the stack in order with `char` widened to a 2-byte `int`,
which is what the library's pointer-walking `va_arg` needs; struct offsets
used by the assembly match gcc's layout.

### Assembly

About 1,100 lines of ACK assembly had to become GNU `as` input.
`tools/ack2gas.py` does the mechanical part: comment character, directive
names (`.define` to `.globl`, `.data2` to `.word`, `.sect`), and one
leading underscore dropped from C names, since ACK prefixed C symbols with
`_` and m68k ELF does not. Operands stay in Motorola syntax; GNU `as`
accepts them with `--register-prefix-optional`.

The converter's output assembled at once and was wrong in three ways that
only a review found:

- ACK exported any label beginning with `_` without being told. Five such
  labels (`phys_copy`, `copyclicks` and others) needed `.globl`.
- In the ST boot sector, GNU `as` chose a word-sized branch where ACK used
  a short one, moving the fixed-offset fields after it by 2 bytes. The
  branch size is now forced.
- ACK had a `.end` section that placed the symbol `end`. Converted, `end`
  landed in `.data`. The linker script defines `etext`, `edata` and `end`
  now.

The assembly was also read against gcc's calling convention: it preserves
`d2-d7/a2-a6` and takes arguments at `-mshort` widths.

### Executable format and host tools

Minix-ST executables have a 32-byte header, text, data, and relocation in
GEMDOS format (one byte per relocated long, giving the distance from the
previous one). ACK's `cv` produced this from ACK object files.
`tools/elf2mnx.py` produces it from an ELF file linked at 0 with `ld -q`,
which keeps the relocations in the output. It refuses what the format
cannot express: 16-bit absolute references, odd addresses, a relocation at
offset 0.

The tree's own build tools could not simply be compiled on the host:
`build.c` assumes a 4-byte `long`, and the host is LP64. They were
rewritten in Python: `build.py` (boot image), `mkfs.py` (Minix V1 file
system from a prototype file), and later `minixfs.py`, which lists, reads
and consistency-checks a Minix file system inside a ProFile image.
`minixfs.py check` was tried against three deliberately damaged images
before its "clean" was believed, and it agrees with Minix's own `fsck` on
the counts.

## A reference build before any Lisa code

Before anything Lisa-specific was written, the unmodified Atari tree was
built with the new toolchain and booted to a shell under Hatari. It still
builds and passes (`make hatari-shell`).

This found the `size_t` and `volatile` problems on a machine where the
kernel and drivers were known to be right, so they were compiler problems
by elimination. On the Lisa, where the drivers were new and the emulator
less tested, the same symptoms would have had three suspects. Later it
answered "is it Minix or is it the Lisa" several times, and it was used
once to run a piece of suspect Lisa code on a second emulator.

## The Lisa as a target

The facts the port depends on, with where each came from. "LisaEm" means
observed there or read in its source; none has been confirmed on hardware.

| Fact | Source |
|---|---|
| Physical RAM ends at 2 MB and does not start at 0 on a 1 MB machine (0x080000). The ROM maps it at logical 0 in MMU context 0 and leaves the physical base at 0x2A4 and the logical end at 0x2A8. | LisaEm, test kernel output |
| The ROM boots a ProFile only if block 0's tag bytes 4-5 are 0xAAAA. It loads the tag at 0x1FFEC and the data at 0x20000 and calls it. | LisaEm `romless.c` |
| ROM ProFile read routine at 0xFE0090: `d1` block, `d2` timeout, `d3` retries, `d4` threshold, `a1` tag buffer, `a2` data buffer. | UniPlus boot blocks |
| MMU: 128 segments of 128 KB, each with an origin and a limit in 512-byte pages; four contexts; supervisor mode always uses context 0; register writes go to the context the SEG latches select. | UniPlus `mmu.h`, LisaEm `mmu.c` |
| In MMU setup mode, offsets below 0x4000 in every segment decode as ROM space and 0x8000-0xBFFF as the MMU registers. Code running in setup mode must sit where address bit 14 is set, and cannot rely on a stack elsewhere. | LisaEm, UniPlus `mch.s` |
| Interrupt levels: 1 vertical retrace, parallel VIA and floppy; 2 COPS (keyboard, mouse, clock); 6 SCC. | UniPlus `ivec.s` |
| The COPS VIA's registers are 2 bytes apart, the parallel VIA's 8. | UniPlus `cops.h`, `pport.h` |
| The screen is 720 x 364, one bit per pixel, a set bit black, at the physical address in the video latch times 32 KB. | UniPlus `bm.c` |
| The floppy is run by a 6504 through shared RAM at 0xFCC000, odd addresses only, and interrupts on level 1 with a flag in the COPS VIA's port B. | UniPlus `sony.c`, LisaEm `floppy.c` |

The 60 Hz clock tick is the vertical retrace interrupt, acknowledged the
way UniPlus does it. No VIA timer is used.

### Disk image and boot

One file serves LisaEm and ESProFile: 532-byte blocks (20 tag bytes, 512
data), block N at offset N x 532, and exactly 5,175,296 or 10,350,592
bytes, because ESProFile derives the drive's spare table from the file
size and knows only those two. ESProFile boots whatever is named
`profile.image`. The image boots on its own; there is no chain loader.

| Blocks | Contents |
|---|---|
| 0 | boot block, tag 0xAAAA |
| 1 | header: magic, image start and length, load and entry address, file system start and length |
| 2... | kernel, MM, FS, INIT, and optionally a RAM disk root |
| rest | Minix V1 file system (`/dev/hd0`) |

Because the header carries the numbers, neither the boot block nor the
kernel has a disk layout compiled in.

The boot block (`boot/lisaboot.S`) is position independent and under 512
bytes. It saves the three ROM globals in registers, reads the image into a
staging area at 0x40000 with the ROM routine, copies it down to 0, and
jumps. The image cannot be read straight to 0: the ROM keeps its globals
in low memory while its read routine is in use.

The boot block also held the bug that cost the most time, described under
"Blockers".

For the RAM disk image, `build.py` appends the root file system directly
after INIT. That is the address MM will give the RAM disk when FS asks for
it, so the RAM disk is loaded by the boot block and never copied.

## The kernel

`MACHINE == LISA` sits beside `ATARI` in `config.h`. Shared files change
only under that switch, and outside the MMU work the changes are a few
lines each. Reference: `docs/lisa-kernel.md`.

**Vectors and context switch** (`lisampx.s`, from `stmpx.s`). The ST code
stored each vector's trap number in the top byte of the handler address in
the vector table, and recovered it from a return address; it worked
because the 68000 ignores the top 8 address bits. On the Lisa every vector
has a stub that stores its number explicitly. The ST ran with interrupt
mask 2 to keep out its horizontal blank interrupt; on the Lisa levels 1
and 2 are the clock and keyboard, so the running mask is 0.

**Console.** Output goes to the Lisa screen and to serial port B; input
comes from both. The plan had a choice at boot. Mirroring cost less code
than choosing, kept the serial-based tests working once the screen existed,
and gives a real Lisa a way to report when its screen shows nothing. The
screen driver is `stvdu.c` less colour and sound. The ST's 8 x 8 font with
one blank line below gives 90 x 40 cells on 720 x 364, the same text size
as UniPlus, and since a set bit is black the font copies in unchanged as
black on white.

**Keyboard.** The COPS sends one byte stream carrying key transitions,
mouse movement, reset codes and clock readings, and the driver is a state
machine over it. The real-time clock is read by sending the COPS a command
and decoding a six-byte reply from that same stream. Auto-repeat counts
clock ticks, as on the ST.

**ProFile.** A polled driver written from Apple's protocol description and
checked against UniPlus's standalone driver. A Minix disk task may block,
so there are no interrupts: each phase is a handshake on /CMD and /BSY. One
ProFile block per request, five tries per block.

**Floppy.** The Sony 400K drive, through the 6504's shared RAM, with an
interrupt on completion. Level 1 is shared with the clock, so the level 1
handler asks the floppy first and counts a tick only if the floppy flag is
clear. No format, no eject, no 800K.

One bug in shared code turned up on the way: `rs_flush` in Minix-ST
returns with interrupts locked when there is nothing to flush. It is fixed
in the Lisa copy only.

## Using the MMU

Minix-ST had no relocation hardware. Every process ran at its physical
address, and fork worked by shadowing: parent and child share one address
range, and whenever the other one has to run the kernel swaps data and
stack with a spare copy. The plan kept shadowing until the system worked
and replaced it afterwards, which meant the MMU work started from a system
with a test suite and timings. Reference: `docs/memory-map.md`.

The scheme:

- Supervisor mode is context 0, left exactly as the ROM set it: all RAM at
  logical 0. Every "physical" address inside Minix is a context 0 address,
  so the kernel still reaches any process's memory directly and no driver
  changed.
- User programs run in context 1. `restart` reloads its segment registers
  from the process's memory map when a different process is about to run.
- Programs are linked at 0 and now run at virtual 0, so exec does no
  relocation. Fork is a copy of the image to new memory.
- MM and FS have contexts 2 and 3, loaded once.

The places in shared code that assumed virtual equals physical were
`umap`, message copying in `cp_mess`, fork, exec's choice of address, and
one stack check. They were found by reading every kernel and MM path that
touches user memory before writing anything, and the MMU kernel ran
correctly the first time it booted.

One mismatch was caught at the design stage: the MMU counts 512-byte
pages and Minix allocates 256-byte clicks. MM now allocates even numbers
of clicks at even click addresses, and the boot image pads each part to
512 bytes. Without that, the first fork would have been mapped half a page
off.

A bus error in user mode becomes SIGSEGV, and MM does not let it be caught
or ignored, because a 68000 cannot resume the instruction.

Rejected: read-only text (Minix keeps text and data in one address range,
so they share segments, and protection is per segment); a context per
process (the MMU has three user contexts); MM and FS in supervisor mode
(they could not be preempted, and their stacks would take interrupts).

Results, 1 MB, same emulator build, same test programs:

| | Shadowing | MMU, one user context | MMU, MM and FS in their own |
|---|---|---|---|
| System call tests, total | 1008 s | 460 s | 356 s |
| `test13` (fork-heavy) | 324 s | 28 s | |
| `test6` (few forks) | 13 s | 32 s | 16 s |
| Write outside own memory | lands in another process | SIGSEGV, system continues | same |

The middle column is why the third exists. With one user context, every
system call reloaded it twice, once to enter MM or FS and once to come
back. With three, a call costs two latch writes each way. A different
theory, that reloads for the IDLE task were the cost, was tested and made
no measurable difference, and the change was taken out again.

The shadowing baseline was measured on a kernel built in a separate git
worktree with the same new test programs. Doing that first exposed three
faults in the tests and benchmark themselves (two in the optimizer table
above, and `times()` returning no elapsed time in Minix 1.5), which would
otherwise have shown up first on the MMU kernel and been blamed on it.

## Testing in an emulator with nobody at it

Every test is a `make` target that boots an image in LisaEm, types, reads
the results and checks them. LisaEm is an interactive GUI program, and
making it do this was a larger share of the work than any one driver. What
it took, all now in upstream LisaEm as environment variables:

| Need | Problem | Answer |
|---|---|---|
| Not to stall | modal alerts block the emulator thread (found with `sample`) | `LISAEM_NO_DIALOGS` logs them and takes the default |
| To see the screen | window capture needs permissions a terminal session lacks | `LISAEM_SCREEN_DUMP` writes a PNG about once a second |
| To read the screen | | `tools/lisascreen.py` matches each character cell against the kernel's own font and returns text, so tests compare the screen exactly |
| To type | | `LISAEM_KEYBOARD_FILE` feeds LisaEm's paste-to-keyboard path, with an escape for raw COPS codes so a key can be held down |
| A serial console | the `File` and `Pipe` settings are not implemented; the pseudo-terminal altered the output | TelnetD, read as raw TCP |
| To leave the user's setup alone | wx names its preferences file after the executable, and a second instance would overwrite the first's | run a renamed copy of the binary; hash the user's preferences before and after |
| To get past the ROM's STARTUP FROM menu | | LisaEm's `-d` |
| To insert a floppy in a running guest | `-f` restarts from the disk | `LISAEM_FLOPPY_AT` |
| To debug | | `LISAEM_CPU_TRACE`: instruction trace with registers, a start trigger on a PC value, a watch on one RAM byte, a RAM dump |

Other things the test runner had to learn: LisaEm throttles serial input,
so it sends one character every 0.3 s; typed input is lost while the
polled disk driver is busy, so long command sequences run from a script on
the disk image; and an emulator that has crashed looks, to a runner
waiting for output, exactly like a test that did not produce it, so check
for the process exiting first.

A warning for anyone building LisaEm: `build.sh` with any option that adds
a define (`--allow2mbram`, `--debug`) starts the new binary under a
debugger with the default configuration, which powers on with whatever
disk images the user normally runs. Here that held a UniPlus disk open for
ten minutes. Build with `GDB=/usr/bin/true`.

## Blockers

The problems that stopped progress, as opposed to those fixed on sight.

| Symptom | Cause | Found by |
|---|---|---|
| Kernel reads wrong bytes, jumps to 0, restarts; changes with code layout and stack position; identical code correct under Hatari | The boot block pointed the ROM's tag buffer 20 bytes below each block's data buffer, so every block's tag overwrote the last 20 bytes of the block before it | CPU trace with a RAM watch: a byte zeroed by the boot block's own loader |
| Wrong arguments in library calls, no pattern | `size_t` width, above | Reading generated assembly |
| Output stops for good in the middle of `ls`, one run in six | Keyboard handler read the COPS "while data is ready". Once the mouse pointer has crossed LisaEm's window, LisaEm always has data: each read makes up another mouse report | Reading LisaEm's COPS code, then a hook that moves the mouse once, which made it happen every time |
| After fixing that: bus error panic on the first keystroke | LisaEm delivered a pending interrupt from inside the write that acknowledges the vertical retrace; the instruction then added its own length to the PC, which by then was the handler's address, so the handler started 8 bytes in | CPU trace triggered at the first keystroke |
| LisaEm itself dies when `test18` runs | Its instruction pre-decoder read past an array when an operand equalled the last address of the code page. In page 0 that is 0x1FF, which is 0777, the Unix permission mask. No earlier guest had run code in page 0 | Unstripped `-g` build, `atos` on the macOS crash report |
| `mount /dev/fd0` fails on a disk that `dd` reads perfectly | The floppy task waited for its interrupt with `receive` into the same global message buffer that held the request. `dd` worked because FS's read-ahead hands the driver a copy | Printing the superblock FS received (it was 68000 code), then the buffer straight after the driver's copy |
| `date` one day behind | LisaEm seeded the COPS day-of-year from the host's 0-based `tm_yday`; UniPlus and LisaEm's own rollover count from 1 | Comparing against UniPlus `rtc.c` |

The first row is the instructive one. The evidence pointed at the
emulator: the same instructions ran correctly on another emulator, the
serial path was clean, and moving the stack changed the symptoms. It was
written up as a LisaEm fault, and two hours of reasoning from symptoms did
not move it. Two runs with an instruction trace did. UniPlus's boot block
never moves its tag pointer, which in hindsight was a hint.

The LisaEm fixes this project needed are upstream (#66, #67, #68, #72,
#75); a wrong key code for `|` is still open. Two of the faults were found
only because Minix did something no earlier guest had: run code at logical
address 0, and take COPS interrupts with the VIA's interrupt enable
actually set. Expect this
with any emulator that has mostly run one operating system. Where a fault
affected Minix it was worked around in Minix as well (interrupts are
masked around the retrace acknowledge), since a real Lisa does not care
and older LisaEm builds exist.

## Open

- A real Lisa. `docs/esprofile.md` has the procedure, ten tests in order,
  and the assumptions to suspect for each kind of failure. The likeliest:
  the ROM read routine's register conventions on other ROM revisions,
  ProFile timeouts tuned in an emulator that answers instantly, a floppy
  driver that has never waited for a real seek, and the RAM layout of a
  real 2 MB machine.
- The console test's last screen comparison currently fails (`# # sync`
  where one prompt is expected), with old and new LisaEm builds alike. Not
  investigated.
- MM, FS and INIT start in the middle of a segment, and a segment limit
  bounds only the top, so they can reach memory below themselves. Exec'd
  programs cannot.
- The COPS clock is read but never set. No printer, mouse, second serial
  port, bell, or non-US keyboard. ProFile on the built-in port only.
- Self-hosting needs the ACK compiler passes as Minix binaries. The
  Minix-ST tree has only the `cc` driver. MacMinix's distribution archive
  may have them; it has not been unpacked.

## If you use other tools

**Another compiler.** Whatever it is, establish the integer model from the
assembly and headers before compiling anything, then check what `sizeof`
and pointer subtraction produce when passed to an unprototyped function.
ACK would avoid the width and optimizer problems altogether, at the price
of building ACK and keeping its object and assembly formats. LLVM's m68k
backend was not tried. With any optimizing compiler the `volatile` audit
is required, and the ST drivers in this tree have had it.

**Another emulator.** The minimum that made this workable: a serial port
reachable from a host program, scripted keyboard input, a way to read the
screen, and an instruction trace with a start trigger and a memory watch.
Without the trace, the boot block bug would have stayed an "emulator bug"
indefinitely. Budget for adding these yourself.

**Another ProFile emulator.** The image is a plain raw image with tags.
Anything that serves 532-byte blocks in drive order should take it,
possibly at a different file size. Only LisaEm has been tried.

**Another machine with a segment MMU.** The decision that kept the MMU
work small was leaving the kernel's context as the ROM set it and treating
Minix's existing "physical" addresses as addresses in that context. Only
user mode became virtual, and only five places in shared code had to know.

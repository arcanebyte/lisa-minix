# Toolchain

Status: phase 0 complete (14 September 2026). The patched cross compiler
builds the Atari Minix-ST tree, which boots to a shell under Hatari
(`docs/hatari.md`).

## Summary

| Tool | Version | Source |
|---|---|---|
| `m68k-minix-gcc` | gcc 16.2.0 + `toolchain/gcc-16.2.0-mshort-size_t.patch` | built by `toolchain/build-gcc.sh` into `~/opt/m68k-minix` |
| m68k-elf-binutils | 2.47 | Homebrew `m68k-elf-binutils` |
| `tools/elf2mnx.py` | this repo | ELF to Minix-ST executable converter |
| `tools/build.py` | this repo | boot image builder (port of `src/tools/build.c`) |
| `tools/mkfs.py` | this repo | Minix V1 file system builder (port of `src/commands/mkfs.c`) |
| `tools/ack2gas.py` | this repo | mechanical ACK to GNU assembly conversion (already applied) |
| `toolchain/minix.ld` | this repo | linker script for Minix-ST executables |
| Host | macOS 26 (Darwin 25.5.0), arm64 | |

Setup:

```
brew install m68k-elf-binutils gmp mpfr libmpc isl zstd
toolchain/build-gcc.sh
```

Homebrew's own `m68k-elf-gcc` is not used for Minix code (see "Why a
patched gcc").

## Integer model (plan decision 2): 16-bit int

The tree was built with ACK in its 16-bit `int` model:

- `include/limits.h`: `INT_MAX 32767`, `UINT_MAX 0xFFFF`.
- `lib/atari/sendrec.s`: `send(dest, ptr)` reads `dest` as a word at
  `4(sp)` and the pointer as a long at `6(sp)`.
- `lib/atari/crtso.s`: pushes `argc` and the exit status with
  `move.w`.
- `include/sys/types.h`: `typedef unsigned int size_t`;
  `include/stddef.h`: `typedef int ptrdiff_t`.
- Pointers and `long` are 32 bits.

## Flags

```
m68k-minix-gcc -mcpu=68000 -mshort -O -std=gnu89 -D_MINIX_KR \
    -fno-builtin -fno-tree-loop-distribute-patterns \
    -nostdinc -Isrc/include
```

plus each directory's Makefile flags: `-DACK` for `kernel`, and
`-D_MINIX -D_POSIX_SOURCE` for `lib/ansi`, `lib/posix` and `commands`.

- `-mshort`: 16-bit `int`.
- `-std=gnu89`: the tree is K&R C.
- `-D_MINIX_KR`: keep `_ANSI` at 0 (`src/include/ansi.h`). ACK was not
  an ANSI compiler, so every `_PROTOTYPE` was an old-style declaration
  and every call used K&R argument promotion. gcc defines `__STDC__` as
  1, which turned the prototypes on and exposed mismatches with the K&R
  definitions in the library.
- `-fno-builtin`: gcc must not replace the library's own `memcpy`,
  `strcpy` and similar with builtins whose prototypes differ from the
  K&R definitions (683 `-Wbuiltin-declaration-mismatch` warnings
  without it).
- `-fno-tree-loop-distribute-patterns`: stops gcc from turning a loop
  in the library's own `memset` or `memcpy` into a call to itself.
- `-nostdinc`: only the Minix headers.

Link with `m68k-elf-ld -q --no-warn-rwx-segments -T toolchain/minix.ld`,
start-up object first, then libraries and
`$(m68k-minix-gcc -print-libgcc-file-name)`.

## Why a patched gcc

Stock gcc `-mshort` makes `int` 16 bits but leaves `size_t` and
`ptrdiff_t` at 32 bits (`__SIZE_TYPE__` `long unsigned int`,
`__PTRDIFF_TYPE__` `long int`). The Minix code was written against an
ACK whose `sizeof` was 16 bits: `tools/init.c`, which ran on the ST,
calls `write(fd, (char *) &utmp, sizeof(struct utmp))` with no cast, as
do 82 of the 83 `read`/`write` calls with a `sizeof` argument in
`commands/`. Under K&R calling, stock gcc pushes such values as 4 bytes
where the callee reads 2, shifting every later argument, with no
diagnostic. Observed with stock gcc 16.2.0:

| Source | Stock `-mshort` | Patched `-mshort` |
|---|---|---|
| `kr(3, buf, sizeof(buf), 5)` | `pea 10` (4 bytes) | `movew #10,sp@-` |
| `kr(a - b)`, two `char *` | `movel d0,sp@-` | `movew d0,sp@-` |
| `x = y` for a 200-byte struct | `memcpy` with `pea 200` | `memcpy` with `movew #200,sp@-` |

`toolchain/gcc-16.2.0-mshort-size_t.patch` makes two changes:

1. `gcc/config/m68k/m68kemb.h` (m68k-elf and RTEMS targets): with
   `-mshort`, `SIZE_TYPE` is `unsigned int` and `PTRDIFF_TYPE` is `int`.
   `SIZETYPE`, gcc's internal type for offsets, stays `long unsigned
   int` so that address arithmetic on 32-bit pointers is not truncated.
   Without `-mshort` nothing changes.
2. `gcc/expr.cc`: the `memcpy`, `memmove`, `memcmp` and `memset` calls
   gcc emits itself (struct copies and clears) pass the length as
   `size_t` instead of the internal `sizetype`. On targets where the two
   are the same, which is every stock configuration, this changes
   nothing.

Consequence, as with ACK: a single `sizeof`, struct copy or pointer
difference cannot exceed 65535 bytes.

`build-gcc.sh` configures with `--with-cpu=68000 --disable-multilib`
and `CFLAGS_FOR_TARGET="-O2 -mcpu=68000 -mshort"`, so the one libgcc
is built for the 68000 with 16-bit `int`. Programs are installed as
`m68k-minix-*` so they cannot be confused with Homebrew's
`m68k-elf-gcc`.

Checked and fine: gcc returns pointers in `d0`, like ACK, so functions
called without a declaration still get pointer results.

## Compile status of the tree

Each `.c` file compiled alone, 14 September 2026.

Unmodified tree with stock `-mshort` flags (no `-D_MINIX_KR`, no
Makefile defines):

| Directory | Compiled | Failed | Cause of failures |
|---|---|---|---|
| `kernel` | 16 | 4 | a function used before its `static` definition, so gcc assumed `extern` |
| `mm` | 11 | 0 | |
| `fs` | 20 | 0 | |
| `lib/ansi` | 58 | 6 | K&R definitions against ANSI prototypes |
| `lib/posix` | 52 | 6 | same, plus `getcwd.c` static-after-extern; `execlp.c` needs `-D_POSIX_SOURCE` |
| `lib/other` | 49 | 1 | K&R definition against ANSI prototype |

After forward declarations in `kernel/proc.c`, `stvdu.c`, `system.c`,
`tty.c` and `lib/posix/getcwd.c`, and with `-D_MINIX_KR` and the
Makefile defines: every `.c` file in `kernel` (20), `mm` (11), `fs`
(20), `lib/ansi` (64), `lib/posix` (58) and `lib/other` (50)
compiles. `tools/build.c` and `tools/fakeunix.c` are host tools,
replaced by `tools/build.py`. Of `commands/`, only `sh`, `login`, `ls`,
`cat`, `echo`, `pwd`, `mkdir`, `rm`, `cp` and `sync` are built so far.

Still to review: 82 calls to undeclared functions
(`-Wimplicit-function-declaration`) and 22 `-Wreturn-type` warnings.
Under K&R rules these behaved the same with ACK, but any undeclared
function returning `long` is truncated to 16 bits at the call.

## Hardware registers

ACK did no dead-store elimination; gcc does. The ST drivers reach
hardware through casts of fixed addresses (`src/kernel/stdma.h` `DMA`,
`stmfp.h` `MFP`, `stacia.h` `KBD`/`MDI`, `stram.h`, `stsound.h`,
`stvideo.h`) with no `volatile`, so gcc treated, for example, the two
writes to the DMA mode register in `dmacomm()` as one and dropped the
first. Floppy reads then failed with `dma status` errors. These macros
now cast to `volatile` structs. `_VOLATILE` from `ansi.h` cannot be used
because it is empty under `-D_MINIX_KR`.

Rule for new code, including every Lisa driver: all device register
access goes through `volatile` pointers.

Not fixed: the drivers' busy-wait delays (`while (--delay >= 0);` in
`stdma.c` and others) count a non-`volatile` local, which gcc removes.
Hatari does not need them; real ST hardware might. The Lisa drivers
must not rely on empty loops for timing.

## Other optimizer traps found later

- **Variables changed by signal handlers.** `src/test/test1.c` waits with
  `while (glov == 0);` for a handler to set `glov`; gcc -O reads `glov`
  once and loops forever. The Minix tests are therefore built with `-O0`
  (Makefile, `$(B)/test/%.o`). Kernel variables changed by interrupt
  handlers must be `volatile` (phase 1).
- **Loops bounded by 32768.** With 16-bit `int`, `for (int j = 0; j <
  sizeof(buf); j++)` over a 32768-byte buffer cannot end without signed
  overflow, which C leaves undefined, so gcc compiles an endless loop
  (found in `tests/forkbench.c`, which then ran off the end of memory).
  Use `unsigned` or `long` for such counters.

- **Undeclared functions that return pointers.** With 16-bit `int`, a
  call to an undeclared `malloc`, `realloc` or `tgetstr` keeps only the low
  16 bits of the pointer. The build uses `-w`, so nothing warns. An audit
  with `-Wint-conversion -Wint-to-pointer-cast` over the commands, the
  library, MM, FS and the kernel (15 September 2026) found five real cases,
  all now declared: `cron.c`, `unshar.c`, `indent/indent.c`,
  `indent/comment.c` (`malloc`, `realloc`) and `nroff/main.c` (`tgetstr`).
  The other hits are deliberate casts of small integers (`exec.c`,
  `signal.c`, `mknod.c`, `syslib.c`, `fs/main.c`). The same programs had the
  same bug under ACK; on the Lisa, programs at virtual address 0 with small
  heaps would often have survived it by luck.

## Variable arguments

The library walks argument lists by pointer: `fprintf(file, fmt, args)`
passes `&args` to `_doprintf`, and `include/stdarg.h` defines `va_arg`
as pointer arithmetic in `int`-sized steps. On the 68000 gcc passes
every argument on the stack, in order, with `char` widened to a 2-byte
`int` under `-mshort`, so this layout is compatible.

## Executable format

Minix-ST programs, and the kernel, mm, fs and init pieces read by
`tools/build.c`, have a 32-byte header of 8 big-endian longs
(`tools/outmix.h`, `mm/exec.c` `read_header`):

| Long | Contents |
|---|---|
| 0 | `0x04100301` (combined I & D) |
| 1 | `0x00000020` |
| 2 | text size (code and read-only data) |
| 3 | initialized data size |
| 4 | bss size |
| 5 | entry point, must be 0 |
| 6 | total memory: text + data + bss + stack/malloc area |
| 7 | symbol table size |

Then text, data, the symbol table (skipped by the loader), and
GEMDOS-format relocation: a long giving the offset from the start of
text of the first long to relocate (0 = none), then one byte per
further one: an even value is the distance in bytes to it, 1 advances
254 bytes, 0 ends the list, other odd values are illegal. Only 32-bit
absolute references can be relocated (`mm/exec.c:423-442`,
`tools/build.c:195`).

`tools/elf2mnx.py` produces this from an ELF file linked with
`toolchain/minix.ld` and `-q`, replacing ACK's `cv`
(`commands/atari/cv.c`). It follows `cv`:

- text size runs to the start of `.data`, data to the start of `.bss`;
- the default stack+malloc area is 64 KB minus text, data and bss, plus
  64 KB until positive; `+N`, `-N`, `=N` adjust it;
- no symbol table is written;
- `-R` writes an empty relocation list (the kernel Makefile uses
  `cv -R`);
- PC-relative relocations and references to absolute symbols (I/O
  addresses) are not relocated.

It fails on relocations the format cannot hold: 16- or 8-bit absolute
references, odd addresses, and a relocation at offset 0. Tested 14
September 2026 with small programs: text and data relocations, a
604-byte gap (encoded 1, 1, 96), `-R`, `=8192`, and the 16-bit error.

C symbols have no leading underscore in m68k ELF, whereas ACK added
one; the assembly conversion drops the underscores (`_main` becomes
`main`), and `minix.ld` defines `etext`, `edata` and `end`.

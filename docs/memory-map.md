# Lisa memory map for Minix

Status: 14 September 2026. Measured in LisaEm (1 MB and 2 MB, boot ROM H)
by the phase 1 test kernel; not yet checked on a real Lisa or against the
Lisa Hardware Manual. Addresses and register layouts are in
`src/kernel/lisa/lisaaddr.h`, with their sources.

## As the boot ROM leaves it

Physical RAM ends at the 2 MB boundary; the ROM finds where it starts
(global 0x2A4) and maps it contiguously at logical 0 in context 0. The
logical end of RAM is in global 0x2A8.

| Configuration | Physical RAM | Logical | Segments | ROM 0x2A8 |
|---|---|---|---|---|
| 1 MB | 080000-17FFFF | 000000-0FFFFF | 00-07, origins 0x400-0xB00 pages | 0x100000 |
| 2 MB (LisaEm `--allow2mbram`, 2 MB less 128 KB) | 000000-1DFFFF | 000000-1DFFFF | 00-0E, origins 0x000-0xE00 pages | 0x1E0000 |

In both, segment 7E (FC0000) is I/O and 7F (FE0000) special I/O (ROM);
all other segments are invalid, and reading 0x800000 gives a bus error.
A segment's limit register is 0x700 (read-write, whole segment) for RAM.

When the ROM starts a program the video page is near the top of physical
RAM (in LisaEm `maxlisaram - 0x10000`), inside the range the ROM reports.
Minix (phase 4, `src/kernel/lisa/lisavdu.c`) moves it to the top 32 KB of
that range by setting the video latch, as UniPlus does, and hands
processes only the memory below it:

| Configuration | Video page (logical) | Latch | Minix memory |
|---|---|---|---|
| 1 MB | 0F8000-0FFFFF | 0x2F | 000000-0F7FFF (992 KB) |
| 2 MB (LisaEm) | 1D8000-1DFFFF | 0x3B | 000000-1D7FFF |

Low memory 0x000-0x7FF holds the ROM's globals while the ROM runs: boot
device at 0x1B3, physical RAM start at 0x2A4, logical end of RAM at 0x2A8.
The boot block reads these before overwriting low memory with the kernel.

## During boot (`boot/lisaboot.S`)

| Logical | Use |
|---|---|
| 01FFEC-01FFFF | block 0 tag (loaded by the ROM) |
| 020000-0201FF | boot block (loaded by the ROM) |
| 03FC00 | boot block stack (grows down) |
| 03FDEC-03FDFF | tag bytes of every block read |
| 03FE00-03FFFF | header block (block 1) |
| 040000- | image staging area, one ProFile block per 512 bytes |
| after the image | the 8-byte copy routine |
| 000000- | image destination (load address from the header) |

The copy runs upwards, which is safe because the destination is below the
staging area. An image larger than about 800 KB would not fit a 1 MB
Lisa's staging area; the boot block does not check this yet.

## Phase 1 test kernel (`toolchain/lisa-test.ld`)

| Logical | Use |
|---|---|
| 000000-0003FF | exception vectors |
| 000400- | text and read-only data |
| next address with bit 14 set (0x4000 today) | `.setup`: code that runs with the MMU in setup mode |
| after `.setup` | data, bss, then a 4 KB kernel stack |

In setup mode the MMU decodes each segment by offset: below 0x4000 special
I/O, 0x8000-0xBFFF the MMU registers, and offsets with bit 14 set go
through the MMU to RAM (LisaEm `mmu.c` `init_start_mode_segment`; UniPlus
reads the registers the same way in `sys/mch.s`). Code that runs in setup
mode must therefore sit at an address with bit 14 set, and must not use
the stack. The linker script moves `.setup` to the next such address and
checks it; with a larger kernel the gap before it can be up to 16 KB, so
the Minix kernel will need a better arrangement.

## Minix with the MMU (phase 5, `src/kernel/lisa/lisammu.c`)

Minix-ST ran every process at its physical address and made `fork` work
by shadowing: parent and child shared one address range, and the kernel
copied data and stack in and out of a spare copy whenever the other one
had to run. On the Lisa the MMU removes that.

**Two contexts.** The 68000's supervisor mode always uses MMU context 0;
user mode uses the context selected by the SEG1 and SEG2 latches.

| Context | Who runs in it | Mapping |
|---|---|---|
| 0 | kernel, tasks, every exception and interrupt handler | as the boot ROM left it: all RAM at logical 0, I/O at FC0000 and FE0000 |
| 1 (SEG1) | INIT, user programs, the IDLE task | loaded for the process about to run |
| 2 (SEG2) | MM | loaded once; reloaded only if MM's map changes |
| 3 (SEG1 and SEG2) | FS | the same |

MM and FS have their own contexts because nearly every system call goes
from a program to one of them and back: switching contexts is two latch
writes, reloading one a register write for each segment (and, in LisaEm,
a flush of its decoded-instruction cache). With a single user context the
Minix system call tests took 460 s; with three, 356 s.

Minix "physical" addresses (clicks in `p_map`, `umap()` results, MM's hole
list) are context 0 addresses, so the kernel reaches any process's memory
directly, as before.

**Loading a context.** `restart` in `lisampx.s` calls `lisa_mmu_switch()`
before returning to a user-mode process. It sets the SEG latches for the
process's context (register writes also go to the latch-selected context),
and if that context does not already hold the process's map, it computes the image (lowest to highest virtual
address of text, data, gap and stack), and for each 128 KB segment the
image touches sets

- origin = (segment's virtual address + delta + ROM `MEMBASE`) / 512,
  where delta = physical minus virtual address, the same for the whole
  image;
- limit = read-write (0x7xx), 256 minus the number of pages used in that
  segment,

and marks every other segment that was valid invalid (0xC00). Register
writes happen in setup mode from `lisa_mmu_write` in `.setup`, which
`toolchain/lisa-kernel.ld` places where address bit 14 is set.

**Virtual addresses.**

| Process | Virtual address of its image | Delta |
|---|---|---|
| program started by exec | 0 (MM, `src/mm/exec.c`) | its physical address |
| child after fork | the parent's | child's physical minus parent's |
| MM, FS, INIT (loaded by the boot block) | their physical address | 0 |
| IDLE | 0, the kernel's image | 0 |

Because programs run at 0, where they are linked, exec does no
relocation, and fork is a copy of the whole image to new memory
(`system.c do_fork`) with no copying afterwards.

**Page alignment.** The origin register counts 512-byte pages; Minix
clicks are 256 bytes. MM allocates and frees memory in even numbers of
clicks at even click addresses (`src/mm/alloc.c`), and `tools/build.py
--lisa` pads kernel, MM, FS and INIT to 512 bytes, so every delta is a
whole number of pages.

**Protection.**

- A program at virtual address 0 can touch only its own image: every
  address above it, the kernel, other processes and the I/O space are
  invalid in its context, and an access is a bus error. The kernel sends
  SIGSEGV, and MM never lets that signal be caught or ignored, because a
  68000 cannot restart the instruction.
- MM, FS, INIT, and INIT's children before they exec, start in the middle
  of a segment. A segment limit bounds only its upper end, so they can
  reach memory below them in that segment (the kernel, for MM). These are
  system processes; exec'd programs are not affected.
- Text is writable. The Minix memory map has text and data in one
  address range, so they share segments, and the MMU protects whole
  segments.

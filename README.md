# Minix 1.5 on the Apple Lisa

Minix 1.5 for the Apple Lisa 2, ported from Minix-ST 1.5, built on a Mac
with a patched cross gcc. Plan: `PLAN.md`.

Status, 15 September 2026:

- **Phase 0 (done):** the Atari ST tree builds with the cross toolchain and
  boots to a shell under Hatari (`docs/toolchain.md`, `docs/hatari.md`).
- **Phase 1 (done in LisaEm):** a boot block and test kernel boot from a raw
  ProFile image, print the MMU set-up on serial port B, count clock ticks
  and report bus errors (`docs/lisaem.md`, `docs/memory-map.md`).
- **Phase 2 (done in LisaEm):** Minix boots from the ProFile image with a
  RAM disk root and runs login and the shell on serial port B
  (`docs/lisa-kernel.md`).
- **Phase 3 (done in LisaEm):** a polled ProFile driver; the root file
  system is on the ProFile with all single-file commands, survives a
  reboot, checks clean with Minix `fsck` and with `tools/minixfs.py` on the
  Mac (`docs/disk-layout.md`).
- **Phase 4 (done in LisaEm):** the console is on the Lisa's own screen
  (90 x 40 text) and keyboard, mirrored on serial port B; tested by typing
  through LisaEm's COPS and reading the screen back as text
  (`docs/lisa-kernel.md`).
- **Phase 5 (done in LisaEm):** the Lisa MMU replaces Minix-ST's
  shadowing: every program runs at address 0 in its own protected memory,
  a stray access kills only that program, and the Minix system call tests
  run in less than half the time (`docs/memory-map.md`,
  `docs/lisa-kernel.md`).
- **Phase 6 (in progress):** the date and time are read from the Lisa's
  clock at boot; the multi-file commands (elvis, mined, make, kermit, nroff
  and others) are built and tested; keyboard auto-repeat; MM and FS have
  MMU contexts of their own; the Sony 400K floppy reads and writes
  (`/dev/fd0`) (`docs/lisa-kernel.md`).
- Nothing has been run on a real Lisa yet.

Write-up material: `docs/journal.md` keeps a dated account of the work,
including problems and wrong turns.

## Building

The build machine is a Mac: macOS 26 (Darwin 25.5.0) on arm64. The
Makefile and scripts use BSD `stat` and `sysctl`, so other hosts would need
small changes.

Requirements:

| What | Version used | Source |
|---|---|---|
| Xcode command line tools | | `xcode-select --install` (clang, make 3.81, curl, patch) |
| Homebrew packages | m68k-elf-binutils 2.47 | `brew install m68k-elf-binutils gmp mpfr libmpc isl zstd` |
| `m68k-minix-gcc` | gcc 16.2.0 with `toolchain/gcc-16.2.0-mshort-size_t.patch` | `toolchain/build-gcc.sh` |
| Python 3 | 3.14 (standard library only) | macOS or Homebrew |

1. Build the cross compiler (downloads gcc from ftp.gnu.org and checks its
   SHA-256; installs to `~/opt/m68k-minix`):

   ```
   toolchain/build-gcc.sh
   ```

   Another install prefix: `toolchain/build-gcc.sh PREFIX`, then pass
   `GCCPREFIX=PREFIX/bin/m68k-minix-` to `make`. Why gcc has to be patched:
   `docs/toolchain.md`.

2. Build the images:

   ```
   make lisa-minix       # build/lisa-minix/minix.image     5 MB, RAM disk root
   make lisa-minix-hd    # build/lisa-minix/minix-hd.image 10 MB, root on the ProFile
   ```

   A plain `make` builds the Atari ST reference system instead (below).
   `make clean` removes `build/`.

The Python tools that build the images, read and check them on the Mac,
make floppies and drive the emulators are described in `TOOLING.md`.

LisaEm is not needed to build either image.

## Running on a real Lisa

Each image is a raw ProFile image, in one of the two sizes ESProFile
accepts. Copy it to an ESProFile SD card as `profile.image` and boot the
Lisa from the ProFile on the built-in parallel port. The 5 MB image is the
safer first test: it only reads from the ProFile. The console is the
Lisa's screen and keyboard, mirrored on a 9600 baud terminal on serial port
B if one is connected. Checklist, and the places a real Lisa could differ
from the emulator: `docs/esprofile.md`.

## Testing in LisaEm (optional)

The `make lisaem-*` targets boot the images in LisaEm without anyone at
the emulator, type commands and check the output. They need upstream
LisaEm (https://github.com/arcanebyte/lisaem) at `3020b09` or later, which
has the environment variables they use, and a Lisa boot ROM H image, which
is not included here. Setup and details: `docs/lisaem.md`.

| Target | Test |
|---|---|
| `make lisaem-minix` | phase 2: boot the 5 MB image, log in, run commands |
| `make lisaem-hd` | phase 3: two boots of the 10 MB image, `fsck`, file checked on the Mac |
| `make lisaem-console` | phase 4: screen and keyboard, at 1 MB and 2 MB |
| `make lisaem-cmds` | multi-file commands, elvis and mined included |
| `make lisaem-floppy` | Sony floppy: `mkfs`, mount, read and write |

## Atari ST reference build (optional)

The tree is Minix-ST 1.5 (`PROVENANCE.md`), and the unmodified Atari build
still builds (`make`) and boots in Hatari (`make hatari-shell`,
`docs/hatari.md`). It is not part of the Lisa images; it is kept as a
reference, to tell Minix or compiler problems from Lisa ones.

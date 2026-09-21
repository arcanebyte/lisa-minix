# Provenance

## src/: Minix-ST 1.5

- Origin: https://github.com/EmmanuelKasper/minix-st-1.5 (archived),
  commit `b618725cab1c98f6e0f1707cb65f7eda66a06c71` (8 February 2016,
  "Initial import from
  http://www.beastielabs.net/download/minix-st-1.5.tar.gz").
- Imported: the repository's `src/` directory, 744 files, copied
  unchanged (checked with `diff -r` against the clone on 14 September
  2026). Every later change to `src/` is a separate commit.
- License: the April 2000 Minix license (BSD 3-clause), see `LICENSE`.

Programs with their own copyright notices, whose terms are in their
source files and are not changed by the Minix license:

| Files | Notice |
|---|---|
| `src/commands/kermit/*` | Trustees of Columbia University 1984–1988; `ckudia.c`, `ckuscr.c` Herman Fischer 1985 |
| `src/commands/indent/*` | Sun Microsystems 1985, University of Illinois 1976, Regents of the University of California 1980 |
| `src/commands/ar/*`, `src/commands/atari/cc.c`, `cv.c`, `src/include/out.h`, `amoeba.h`, `host_os.h` | Vrije Universiteit, Amsterdam, 1988 (ACK) |
| `src/commands/more.c` | Regents of the University of California 1980 |
| `src/commands/patch/patch.c` | Larry Wall 1986 |
| `src/commands/elvis/regexp.c`, `regsub.c`, `src/lib/other/regexp.c`, `regsub.c` | University of Toronto 1986 (Henry Spencer) |
| `src/lib/other/getopt.c` | Henry Spencer |
| `src/commands/du.c`, `src/lib/other/termcap.c` | Joypace Ltd 1987 |
| `src/commands/ed.c` | Brian Beattie 1987 |
| `src/commands/cut.c` | Michael John Holme 1989 |
| `src/commands/de/*`, `src/commands/ic/*` | Terrence W. Holm 1988–1989 |
| `src/commands/fortune.c` | Bert Reuling 1988 |
| `src/commands/paste.c` | David M. Ihnat 1984 |
| `src/commands/roff.c` | G. L. Sicherman 1983 |
| `src/commands/ttt.c` | Warren Toomey 1988 |
| `src/commands/stclock/weidertc.c` | 1988, no holder named |
| `src/lib/ansi/tmpnam.c` | Monty Walls 1989 |
| `src/test/test12.c`, `test13.c` | Martin Leisner 1987 |
| `src/commands/crc.c`, `inodes.c`, `src/commands/zmodem/crctab.c` | CRC macro derived from an article by Stephen Satchell 1986 |

`src/include/minix/const.h` carries the original 1990 Prentice-Hall
notice, superseded by the April 2000 license.

Before distributing binaries built from these programs, check their
terms; kermit's in particular predate open-source licensing.

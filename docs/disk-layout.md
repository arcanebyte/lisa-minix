# ProFile disk layout

Status: 14 September 2026. Used in LisaEm (`make lisaem-hd`); not yet on a
real Lisa.

## Image file

A raw ProFile image as LisaEm and ESProFile read it: 532-byte blocks, each
20 tag bytes then 512 data bytes, block N at file offset N * 532. Exactly
5,175,296 bytes (9728 blocks) or 10,350,592 bytes (19456 blocks). Written
by `tools/mklisa.py`.

| ProFile blocks | Contents |
|---|---|
| 0 | boot block (`boot/lisaboot.S`); tag bytes 4-5 = 0xAAAA |
| 1 | header, big-endian longs: `MNXL`, version 1, first image block, image block count, load address, entry address, first file system block, file system block count |
| 2 ... | boot image: kernel, mm, fs, init (`tools/build.py --lisa`), and a RAM disk root file system if there is one |
| after the image | the Minix file system, if any |

All tags other than block 0's are zero. The Minix driver writes zero tags.

## Minix devices

`src/kernel/lisa/lisapro.c` (`winchester_task`, major 3):

| Device | Minor | Blocks |
|---|---|---|
| `/dev/hd0` | 0 | the file system area from the header (first block, count) |
| `/dev/hd1` | 1 | the whole ProFile; size from the spare table (block $FFFFFF, bytes 18-20) |

Minix file system blocks are 1024 bytes, two ProFile blocks each.

## Images built

| Make target | File | Root |
|---|---|---|
| `make lisa-minix` | `build/lisa-minix/minix.image`, 5 MB | RAM disk loaded from the boot image (360 blocks) |
| `make lisa-minix-hd` | `build/lisa-minix/minix-hd.image`, 10 MB | `/dev/hd0`: 8000-block file system at ProFile block 261, 1000 inodes, all commands in `/bin` |

FS chooses: if `tools/build.py` wrote a RAM disk size into FS's data space,
the root is the RAM disk; otherwise it is `/dev/hd0`
(`src/fs/main.c`, `MACHINE == LISA`).

## Floppy (`/dev/fd0`)

`src/kernel/lisa/lisafloppy.c`, major 2, minor 0: the whole Sony 400K
disk, 800 blocks of 512 bytes, so a Minix file system of 400 blocks
(`tools/mkfs.py` with `400` in the prototype). On the Mac, LisaEm uses
DiskCopy 4.2 images: `tools/mkdc42.py create IMAGE FS` wraps a file system
in one and `tools/mkdc42.py data IMAGE OUT` takes it out again for
`tools/minixfs.py`. `/mnt` exists on the hard disk image to mount it on.

## Host tools

| Tool | Use |
|---|---|
| `tools/mkfs.py` | make a Minix V1 file system from a prototype file |
| `tools/mklisa.py` | assemble boot block, header, boot image and file system into an image |
| `tools/minixfs.py IMAGE ls PATH` | list a directory (ProFile image or plain file system) |
| `tools/minixfs.py IMAGE get PATH [OUT]` | copy a file out |
| `tools/minixfs.py IMAGE check` | consistency check: bit maps, zones claimed twice, link counts, `.` and `..`, zones past the end of a file |

`minixfs.py check` was tested against three deliberately damaged copies
(a cleared zone map bit, a wrong link count, a zone given to two files),
and agrees with Minix `fsck` on the Lisa (same counts of files and free
zones). There is no host tool yet to write files into an existing image.

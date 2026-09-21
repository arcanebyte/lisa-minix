#!/usr/bin/env python3
"""
build.py -- put the Minix-ST boot image together (port of src/tools/build.c).

build.c cannot simply be compiled for the Mac: it fills structures of
C longs through getstruc(), which assumes a 4-byte long. This is a
line-by-line reimplementation of its ATARI_ST configuration. Needs only
Python 3.

USAGE
    python3 tools/build.py BOOTBLOK KERNEL MM FS INIT MENU IMAGE
    python3 tools/build.py --lisa KERNEL MM FS INIT ROOTFS IMAGE

    --lisa: build the image loaded by boot/lisaboot.S (wrap it with
    tools/mklisa.py). No boot sector and no menu; bss is written out in
    full, so the image is contiguous from address 0; the root file system
    image ROOTFS is appended at the end of INIT, which is where MM places the
    RAM disk, and its size in blocks is patched into FS's data space at
    offset 10 (data_org[INFO + 3], read by fs/main.c when MACHINE == LISA).
    ROOTFS "-" means no RAM disk: the size is 0 and FS uses /dev/hd0.

    BOOTBLOK   raw boot block (at most 512 bytes of code, no header)
    KERNEL.. MENU   Minix-ST executables (tools/elf2mnx.py output)
    IMAGE      output file, created or truncated

What it does, as build.c:
  - sector 0: the boot block, padded to 512 bytes;
  - from sector 1: kernel, mm, fs, init and menu, each relocated by the
    total size of the programs before it (menu is not counted), with
    text+data+bss padded to a multiple of CLICK_SIZE (256). Whole
    512-byte sectors of bss are not written; the count of sectors written
    and of zero sectors skipped for each program goes into the boot block
    table at offset 480, where the boot block uses it;
  - patch 1: boot block words 504-510 (sectors to load, menu origin in
    clicks), and the TOS checksum word at 502 so that the 16-bit sum of
    the boot sector is 0x1234;
  - patch 2: the kernel's data space starts with magic 0x526F and gets a
    table of (0, text+data+bss clicks) for kernel, mm, fs and init;
  - patch 3: mm's and fs's data spaces start with magic 0xDADA; fs gets
    init's origin, text clicks (0) and data clicks at offsets 4, 6, 8.
"""

import struct
import sys

PROGRAMS = 5
PROG_ORG = 0
SECTOR_SIZE = 512
KERNEL_D_MAGIC = 0x526F
FS_D_MAGIC = 0xDADA
CLICK_SIZE = 256
CLICK_SHIFT = 8
KERN, MM, FS, INIT, FSCK = 0, 1, 2, 3, 4

A_MAGICD = 0x04100301
SZ_HEAD = 32

NAMES = ["\nkernel", "mm    ", "fs    ", "init  ", "menu  "]


def pexit(s1, s2=""):
    print("Build: %s%s" % (s1, s2))
    sys.exit(1)


class Image:
    def __init__(self, path):
        self.f = open(path, "w+b")
        self.cur_sector = 0
        self.buf = bytearray(SECTOR_SIZE)
        self.buf_bytes = 0

    def write_block(self, blk, data):
        self.f.seek(SECTOR_SIZE * blk)
        self.f.write(bytes(data))

    def read_block(self, blk):
        self.f.seek(SECTOR_SIZE * blk)
        data = self.f.read(SECTOR_SIZE)
        if len(data) != SECTOR_SIZE:
            pexit("block read error")
        return bytearray(data)

    def clear_buf(self):
        self.buf = bytearray(SECTOR_SIZE)
        self.buf_bytes = 0
        self.cur_sector += 1

    def wr_out(self, data):
        pos = 0
        while pos < len(data):
            room = SECTOR_SIZE - self.buf_bytes
            count = min(room, len(data) - pos)
            self.buf[self.buf_bytes:self.buf_bytes + count] = data[pos:pos + count]
            self.buf_bytes += count
            pos += count
            if self.buf_bytes == SECTOR_SIZE:
                self.write_block(self.cur_sector, self.buf)
                self.clear_buf()

    def flush(self):
        if self.buf_bytes == 0:
            return
        self.write_block(self.cur_sector, self.buf)
        self.clear_buf()

    def get_word(self, offset):
        block = self.read_block(offset // SECTOR_SIZE)
        return struct.unpack_from(">H", block, offset % SECTOR_SIZE)[0]

    def put_word(self, offset, value):
        blk = offset // SECTOR_SIZE
        block = self.read_block(blk)
        struct.pack_into(">H", block, offset % SECTOR_SIZE, value & 0xFFFF)
        self.write_block(blk, block)


class Sizes:
    text_size = data_size = bss_size = secs = nulls = 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--lisa":
        return main_lisa(argv[2:])
    if len(argv) != PROGRAMS + 3:
        pexit("seven file names expected. ")

    image = Image(argv[7])
    sizes = [Sizes() for _ in range(PROGRAMS)]
    cum_size = 0
    all_size = 0

    # Copy the boot block.
    try:
        with open(argv[1], "rb") as f:
            boot = f.read()
    except OSError:
        pexit("can't open ", argv[1])
    image.wr_out(boot)
    image.flush()

    # Copy the 5 programs.
    for num in range(PROGRAMS):
        file_name = argv[num + 2]
        try:
            with open(file_name, "rb") as f:
                exe = f.read()
        except OSError:
            pexit("can't open ", file_name)
        if len(exe) < SZ_HEAD:
            pexit("file header too short: ", file_name)
        (a_magic, _versn, a_tsize, a_dsize, a_bsize, a_entry, _msize,
         a_ssize) = struct.unpack_from(">8I", exe, 0)
        if a_magic != A_MAGICD:
            pexit("bad header type. File: ", file_name)
        if a_entry != 0:
            pexit("entry point not 0. File: ", file_name)

        reloshift = cum_size
        reloffset = SZ_HEAD + a_tsize + a_dsize + a_ssize

        tot_bytes = a_tsize + a_dsize + a_bsize
        rest = tot_bytes % CLICK_SIZE
        filler = CLICK_SIZE - rest if rest > 0 else 0
        a_bsize += filler
        tot_bytes += filler
        if num < FSCK:
            cum_size += tot_bytes
        all_size += tot_bytes

        s = sizes[num]
        s.text_size, s.data_size, s.bss_size = a_tsize, a_dsize, a_bsize
        s.secs = image.cur_sector

        if num < FSCK:
            print("%s  text=%5d  data=%5d  bss=%5d  tot=%5d  hex=%4X"
                  % (NAMES[num], a_tsize, a_dsize, a_bsize, tot_bytes,
                     tot_bytes))

        # Relocate text and data (GEMDOS format).
        length = a_tsize + a_dsize
        if len(exe) < SZ_HEAD + length:
            pexit("read error on file ", file_name)
        buf1 = bytearray(exe[SZ_HEAD:SZ_HEAD + length])
        rel = exe[reloffset:]
        if len(rel) < 4:
            pexit("relocation info missing on file ", file_name)
        first = struct.unpack_from(">I", rel, 0)[0]
        if first != 0:
            p1 = first
            p2 = 4
            while True:
                if p1 < 0 or p1 >= length:
                    pexit("bad relocation in ", file_name)
                b4 = struct.unpack_from(">I", buf1, p1)[0]
                struct.pack_into(">I", buf1, p1, (b4 + reloshift) & 0xFFFFFFFF)
                while True:
                    if p2 >= len(rel):
                        pexit("read error on file ", file_name)
                    c = rel[p2]
                    p2 += 1
                    if c != 1:
                        break
                    p1 += 254
                if c == 0:
                    break
                if c & 1:
                    pexit("odd relo byte on file ", file_name)
                p1 += c
        image.wr_out(buf1)

        # Write the bss, skipping whole sectors.
        n1 = n2 = 0
        bsize = a_bsize
        while bsize != 0:
            count = min(bsize, SECTOR_SIZE)
            if count > SECTOR_SIZE - image.buf_bytes:
                count = SECTOR_SIZE - image.buf_bytes
            if count != SECTOR_SIZE:
                image.wr_out(bytes(count))
            elif n1 == 0:
                n1 = image.cur_sector
            else:
                n2 += 1
            bsize -= count
        if n1:
            s.nulls = n2 + 1
            s.secs = n1 - s.secs
        else:
            s.nulls = 0
            s.secs = image.cur_sector - s.secs

    image.flush()
    print("                                               -----     -----")
    print("Operating system size  %29d     %5X" % (cum_size, cum_size))
    print("\nTotal size including menu is %d." % all_size)

    # Patch 1: boot block.
    if cum_size % CLICK_SIZE != 0:
        pexit("MINIX is not multiple of CLICK_SIZE bytes")
    menu_org = PROG_ORG + cum_size
    cs = (menu_org >> CLICK_SHIFT) & 0xFFFF
    sectrs = ((all_size + 511) // 512) & 0xFFFF
    cbuf = image.read_block(0)
    struct.pack_into(">H", cbuf, SECTOR_SIZE - 8, sectrs)
    struct.pack_into(">H", cbuf, SECTOR_SIZE - 6, cs)
    struct.pack_into(">H", cbuf, SECTOR_SIZE - 4, 0)
    struct.pack_into(">H", cbuf, SECTOR_SIZE - 2, cs)
    for i in range(PROGRAMS):
        struct.pack_into(">H", cbuf, SECTOR_SIZE - 32 + i * 4, sizes[i].secs & 0xFFFF)
        struct.pack_into(">H", cbuf, SECTOR_SIZE - 30 + i * 4, sizes[i].nulls & 0xFFFF)
    cbuf[SECTOR_SIZE - 12] = cbuf[SECTOR_SIZE - 11] = 0
    total = sum(struct.unpack(">256H", cbuf)) & 0xFFFF
    old = struct.unpack_from(">H", cbuf, SECTOR_SIZE - 10)[0]
    struct.pack_into(">H", cbuf, SECTOR_SIZE - 10, (old - total + 0x1234) & 0xFFFF)
    image.write_block(0, cbuf)

    # Patch 2: size table in the kernel's data space.
    data_offset = 512 + sizes[KERN].text_size
    if image.get_word(data_offset) != KERNEL_D_MAGIC:
        pexit("kernel data space: no magic #")
    for i in range(PROGRAMS - 1):
        data_clicks = (sizes[i].text_size + sizes[i].data_size
                       + sizes[i].bss_size) >> CLICK_SHIFT
        image.put_word(data_offset + 4 * i, 0)
        image.put_word(data_offset + 4 * i + 2, data_clicks)

    # Patch 3: init's origin and size in fs's data space.
    init_org = PROG_ORG
    init_org += sizes[KERN].text_size + sizes[KERN].data_size + sizes[KERN].bss_size
    mm_data = init_org - PROG_ORG + 512
    mm_data += sizes[MM].text_size
    mm_data -= SECTOR_SIZE * sizes[KERN].nulls
    init_org += sizes[MM].text_size + sizes[MM].data_size + sizes[MM].bss_size
    fs_org = init_org - PROG_ORG + 512
    fs_org += sizes[FS].text_size
    fs_org -= SECTOR_SIZE * (sizes[KERN].nulls + sizes[MM].nulls)
    init_org += sizes[FS].text_size + sizes[FS].data_size + sizes[FS].bss_size
    init_data_size = (sizes[INIT].text_size + sizes[INIT].data_size
                      + sizes[INIT].bss_size) >> CLICK_SHIFT
    init_org >>= CLICK_SHIFT

    if image.get_word(mm_data) != FS_D_MAGIC:
        pexit("mm data space: no magic #")
    if image.get_word(fs_org) != FS_D_MAGIC:
        pexit("fs data space: no magic #")
    image.put_word(fs_org + 4, init_org)
    image.put_word(fs_org + 6, 0)
    image.put_word(fs_org + 8, init_data_size)
    image.f.close()


def read_exe(file_name):
    try:
        with open(file_name, "rb") as f:
            exe = f.read()
    except OSError:
        pexit("can't open ", file_name)
    if len(exe) < SZ_HEAD:
        pexit("file header too short: ", file_name)
    (a_magic, _versn, a_tsize, a_dsize, a_bsize, a_entry, _msize,
     a_ssize) = struct.unpack_from(">8I", exe, 0)
    if a_magic != A_MAGICD:
        pexit("bad header type. File: ", file_name)
    if a_entry != 0:
        pexit("entry point not 0. File: ", file_name)
    return exe, a_tsize, a_dsize, a_bsize, a_ssize


def relocate(exe, a_tsize, a_dsize, a_ssize, reloshift, file_name):
    length = a_tsize + a_dsize
    buf1 = bytearray(exe[SZ_HEAD:SZ_HEAD + length])
    rel = exe[SZ_HEAD + length + a_ssize:]
    if len(rel) < 4:
        pexit("relocation info missing on file ", file_name)
    p1 = struct.unpack_from(">I", rel, 0)[0]
    if p1 == 0:
        return buf1
    p2 = 4
    while True:
        if p1 >= length:
            pexit("bad relocation in ", file_name)
        b4 = struct.unpack_from(">I", buf1, p1)[0]
        struct.pack_into(">I", buf1, p1, (b4 + reloshift) & 0xFFFFFFFF)
        while True:
            if p2 >= len(rel):
                pexit("read error on file ", file_name)
            c = rel[p2]
            p2 += 1
            if c != 1:
                break
            p1 += 254
        if c == 0:
            return buf1
        if c & 1:
            pexit("odd relo byte on file ", file_name)
        p1 += c


def main_lisa(args):
    if len(args) != 6:
        pexit("--lisa: six file names expected. ")
    names = args[0:4]
    rootfs_name, image_name = args[4], args[5]
    out = bytearray()
    sizes = []
    for num, file_name in enumerate(names):
        exe, a_tsize, a_dsize, a_bsize, a_ssize = read_exe(file_name)
        tot = a_tsize + a_dsize + a_bsize
        # Two clicks, the Lisa MMU's 512-byte page: MM must be able to fork
        # INIT to an even click (src/kernel/lisa/lisammu.c).
        filler = (-tot) % (2 * CLICK_SIZE)
        a_bsize += filler
        tot += filler
        print("%s  text=%5d  data=%5d  bss=%5d  tot=%5d  hex=%4X"
              % (NAMES[num], a_tsize, a_dsize, a_bsize, tot, tot))
        body = relocate(exe, a_tsize, a_dsize, a_ssize, len(out), file_name)
        start = len(out)
        out += body
        out += bytes(a_bsize)
        sizes.append((start, a_tsize, a_dsize, a_bsize))
    cum_size = len(out)
    print("                                               -----     -----")
    print("Operating system size  %29d     %5X" % (cum_size, cum_size))

    # Patch 2: size table in the kernel's data space.
    data_offset = sizes[KERN][1]
    if struct.unpack_from(">H", out, data_offset)[0] != KERNEL_D_MAGIC:
        pexit("kernel data space: no magic #")
    for i in range(4):
        _, t, d, b = sizes[i]
        struct.pack_into(">HH", out, data_offset + 4 * i, 0, (t + d + b) >> CLICK_SHIFT)

    # Patch 3: init's origin and size, and the RAM disk size, in fs's data.
    mm_data = sizes[MM][0] + sizes[MM][1]
    fs_data = sizes[FS][0] + sizes[FS][1]
    if struct.unpack_from(">H", out, mm_data)[0] != FS_D_MAGIC:
        pexit("mm data space: no magic #")
    if struct.unpack_from(">H", out, fs_data)[0] != FS_D_MAGIC:
        pexit("fs data space: no magic #")
    init_start, it, idd, ib = sizes[INIT]
    if rootfs_name == "-":
        rootfs = b""
    else:
        try:
            with open(rootfs_name, "rb") as f:
                rootfs = f.read()
        except OSError:
            pexit("can't open ", rootfs_name)
    ram_blocks = (len(rootfs) + 1023) // 1024
    struct.pack_into(">HHHH", out, fs_data + 4, init_start >> CLICK_SHIFT, 0,
                     (it + idd + ib) >> CLICK_SHIFT, ram_blocks)

    out += rootfs + bytes(ram_blocks * 1024 - len(rootfs))
    with open(image_name, "wb") as f:
        f.write(out)
    print("RAM disk: %d blocks at %X; image %d bytes" % (ram_blocks, cum_size, len(out)))


if __name__ == "__main__":
    main(sys.argv)

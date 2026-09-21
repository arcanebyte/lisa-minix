#!/usr/bin/env python3
"""
mkfs.py -- make a Minix V1 file system image on the Mac (port of
src/commands/mkfs.c). Needs only Python 3.

USAGE
    python3 tools/mkfs.py [-b] IMAGE PROTO

    IMAGE   output file, created or truncated to the file system size
    PROTO   prototype file in the format of mkfs.c (below)
    -b      big-endian structures (68000: Atari ST, Lisa); the default.
            -l writes little-endian (Intel) structures instead.

PROTOTYPE FILE (mkfs(8) in the Minix 1.5 manual)
    boot                  line 1: ignored (boot block name)
    360 127               line 2: blocks, inodes
    d--755 2 1            line 3: root directory mode, uid, gid
    bin d--755 2 1        a directory; its entries follow, ended by $
       sh ---755 2 1 build/atari/sh
    $
    dev d--755 2 1
       tty0 c--622 0 0 4 0      character special: major minor
       fd0 b--666 0 0 2 0 360   block special: major minor [size in blocks]
    $
    $                     ends the root directory

    Mode: file type (- d b c), set-uid (u or -), set-gid (g or -), then
    three octal digits. Host file names are relative to the directory the
    tool is run from. Blank lines and lines starting with # are skipped
    (an extension to mkfs.c).

LAYOUT (as mkfs.c, BLOCK_SIZE 1024, zone size = block size)
    block 0 boot block (zero), block 1 super block, inode map, zone map,
    inodes (32 bytes each, 32 per block), data zones. Inode 1 is the root
    directory. Bit 0 of both maps is reserved; bits past the end of the
    maps are set. Bitmaps are arrays of 16-bit words, bit n in word n/16
    at 1 << (n % 16). Files may use the 7 direct zones and one single
    indirect zone, as in mkfs.c.

File modification times are the host files' times; directories and
special files get the time mkfs.py runs, unless SOURCE_DATE_EPOCH is set.
"""

import os
import struct
import sys
import time

BLOCK_SIZE = 1024
INODE_SIZE = 32
INODES_PER_BLOCK = BLOCK_SIZE // INODE_SIZE
NR_DZONE_NUM = 7
NR_INDIRECTS = BLOCK_SIZE // 2
DIR_ENTRY_SIZE = 16
NAME_MAX = 14
SUPER_MAGIC = 0x137F
BITS_PER_BLOCK = 8 * BLOCK_SIZE

I_REGULAR = 0o100000
I_BLOCK_SPECIAL = 0o060000
I_DIRECTORY = 0o040000
I_CHAR_SPECIAL = 0o020000
I_SET_UID_BIT = 0o004000
I_SET_GID_BIT = 0o002000

INODE_MAP = 2


def pexit(msg):
    sys.stderr.write("mkfs: %s\n" % msg)
    sys.exit(1)


class FileSystem:
    def __init__(self, blocks, inodes, endian):
        if blocks > 65535:
            pexit("Block count too large")
        self.e = endian
        self.nrblocks = blocks
        self.nrinodes = inodes
        self.image = bytearray(blocks * BLOCK_SIZE)
        self.now = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
        self.super()

    # --- raw access --------------------------------------------------------

    def block(self, b):
        return memoryview(self.image)[b * BLOCK_SIZE:(b + 1) * BLOCK_SIZE]

    def insert_bit(self, block, bit, count):
        buf = self.block(block)
        for i in range(bit, bit + count):
            off = 2 * (i // 16)
            w = struct.unpack_from(self.e + "H", buf, off)[0]
            struct.pack_into(self.e + "H", buf, off, w | (1 << (i % 16)))

    def inode_pos(self, n):
        b = (n - 1) // INODES_PER_BLOCK + self.inode_offset
        return b * BLOCK_SIZE + (n - 1) % INODES_PER_BLOCK * INODE_SIZE

    def get_inode(self, n):
        pos = self.inode_pos(n)
        f = struct.unpack_from(self.e + "HHIIBB9H", self.image, pos)
        return {"mode": f[0], "uid": f[1], "size": f[2], "mtime": f[3],
                "gid": f[4], "nlinks": f[5], "zone": list(f[6:])}

    def put_inode(self, n, ino):
        struct.pack_into(self.e + "HHIIBB9H", self.image, self.inode_pos(n),
                         ino["mode"], ino["uid"], ino["size"] & 0xFFFFFFFF,
                         ino["mtime"] & 0xFFFFFFFF, ino["gid"], ino["nlinks"],
                         *ino["zone"])

    # --- mkfs.c ----------------------------------------------------------------

    @staticmethod
    def bitmapsize(nr_bits):
        return (nr_bits + BITS_PER_BLOCK - 1) // BITS_PER_BLOCK

    def super(self):
        zones = self.nrblocks
        inodes = self.nrinodes
        imap_blocks = self.bitmapsize(1 + inodes)
        zmap_blocks = self.bitmapsize(zones)
        self.inode_offset = imap_blocks + zmap_blocks + 2
        inodeblks = (inodes + INODES_PER_BLOCK - 1) // INODES_PER_BLOCK
        initblks = self.inode_offset + inodeblks
        self.firstdatazone = initblks
        self.zoff = self.firstdatazone - 1
        max_size = (7 + NR_INDIRECTS + NR_INDIRECTS * NR_INDIRECTS) * BLOCK_SIZE
        if initblks >= self.nrblocks:
            pexit("File system too small for its inodes")
        struct.pack_into(self.e + "HHHHHhIh", self.image, BLOCK_SIZE,
                         inodes, zones, imap_blocks, zmap_blocks,
                         self.firstdatazone, 0, max_size, SUPER_MAGIC)

        self.next_zone = self.firstdatazone
        self.next_inode = 1
        self.zone_map = INODE_MAP + imap_blocks

        bit_map_len = inodes + 1
        residual = bit_map_len % BITS_PER_BLOCK or BITS_PER_BLOCK
        b_needed = self.bitmapsize(bit_map_len)
        self.insert_bit(INODE_MAP + b_needed - 1, residual,
                        BITS_PER_BLOCK - residual)

        bit_map_len = zones - initblks + 1
        residual = bit_map_len % BITS_PER_BLOCK or BITS_PER_BLOCK
        b_needed = self.bitmapsize(bit_map_len)
        b_allocated = self.bitmapsize(zones)
        self.insert_bit(self.zone_map + b_needed - 1, residual,
                        BITS_PER_BLOCK - residual)
        if b_needed != b_allocated:
            self.insert_bit(self.zone_map + b_allocated - 1, 0, BITS_PER_BLOCK)
        self.insert_bit(self.zone_map, 0, 1)
        self.insert_bit(INODE_MAP, 0, 1)

    def alloc_inode(self, mode, uid, gid):
        num = self.next_inode
        self.next_inode += 1
        if num >= self.nrinodes:
            pexit("File system does not have enough inodes")
        self.put_inode(num, {"mode": mode, "uid": uid, "size": 0, "mtime": 0,
                             "gid": gid, "nlinks": 0, "zone": [0] * 9})
        self.insert_bit(INODE_MAP, num, 1)
        return num

    def alloc_zone(self):
        z = self.next_zone
        self.next_zone += 1
        if z + 1 > self.nrblocks:
            pexit("File system not big enough for all the files")
        self.insert_bit(self.zone_map, z - self.zoff, 1)
        return z

    def add_zone(self, n, z, nbytes, mtime):
        ino = self.get_inode(n)
        ino["size"] += nbytes
        ino["mtime"] = mtime
        for i in range(NR_DZONE_NUM):
            if ino["zone"][i] == 0:
                ino["zone"][i] = z
                self.put_inode(n, ino)
                return
        if ino["zone"][NR_DZONE_NUM] == 0:
            ino["zone"][NR_DZONE_NUM] = self.alloc_zone()
        self.put_inode(n, ino)
        blk = self.block(ino["zone"][NR_DZONE_NUM])
        for i in range(NR_INDIRECTS):
            if struct.unpack_from(self.e + "H", blk, 2 * i)[0] == 0:
                struct.pack_into(self.e + "H", blk, 2 * i, z)
                return
        pexit("File has grown beyond single indirect")

    def incr_link(self, n):
        ino = self.get_inode(n)
        ino["nlinks"] += 1
        self.put_inode(n, ino)

    def incr_size(self, n, count):
        ino = self.get_inode(n)
        ino["size"] += count
        self.put_inode(n, ino)

    def enter_dir(self, parent, name, child):
        raw = name.encode()
        if len(raw) > NAME_MAX:
            pexit("name too long: %s" % name)
        ino = self.get_inode(parent)
        for k in range(NR_DZONE_NUM):
            z = ino["zone"][k]
            if z == 0:
                z = self.alloc_zone()
                ino["zone"][k] = z
                self.put_inode(parent, ino)
            blk = self.block(z)
            for off in range(0, BLOCK_SIZE, DIR_ENTRY_SIZE):
                if struct.unpack_from(self.e + "H", blk, off)[0] == 0:
                    struct.pack_into(self.e + "H14s", blk, off, child, raw)
                    return
        pexit("Directory-inode %d beyond direct blocks. Could not enter %s"
              % (parent, name))

    def rootdir(self, inode):
        z = self.alloc_zone()
        self.add_zone(inode, z, 32, self.now)
        self.enter_dir(inode, ".", inode)
        self.enter_dir(inode, "..", inode)
        self.incr_link(inode)
        self.incr_link(inode)

    def eat_file(self, inode, path):
        with open(path, "rb") as f:
            data = f.read()
        mtime = int(os.stat(path).st_mtime)
        for pos in range(0, len(data), BLOCK_SIZE):
            chunk = data[pos:pos + BLOCK_SIZE]
            z = self.alloc_zone()
            self.block(z)[:len(chunk)] = chunk
            self.add_zone(inode, z, len(chunk), mtime)

    def eat_dir(self, parent, lines):
        while True:
            tokens = next_line(lines)
            if tokens[0] == "$":
                return
            name, p = tokens[0], tokens[1]
            mode = mode_con(p)
            uid, gid = int(tokens[2]), int(tokens[3])
            n = self.alloc_inode(mode, uid, gid)
            self.enter_dir(parent, name, n)
            self.incr_size(parent, DIR_ENTRY_SIZE)
            self.incr_link(n)
            if p[0] == "d":
                z = self.alloc_zone()
                self.add_zone(n, z, 32, self.now)
                self.enter_dir(n, ".", n)
                self.enter_dir(n, "..", parent)
                self.incr_link(parent)
                self.incr_link(n)
                self.eat_dir(n, lines)
            elif p[0] in "bc":
                maj, minor = int(tokens[4]), int(tokens[5])
                size = int(tokens[6]) * BLOCK_SIZE if len(tokens) > 6 else 0
                self.add_zone(n, (maj << 8) | minor, size, self.now)
            else:
                if len(tokens) < 5:
                    pexit("no host file for %s" % name)
                try:
                    self.eat_file(n, tokens[4])
                except OSError as err:
                    pexit("Can't open file %s: %s" % (tokens[4], err))


def mode_con(p):
    if len(p) != 6:
        pexit("bad mode %s" % p)
    mode = int(p[3:], 8)
    mode += {"d": I_DIRECTORY, "b": I_BLOCK_SPECIAL, "c": I_CHAR_SPECIAL,
             "-": I_REGULAR}.get(p[0], 0)
    if p[1] == "u":
        mode += I_SET_UID_BIT
    if p[2] == "g":
        mode += I_SET_GID_BIT
    return mode


def next_line(lines):
    for line in lines:
        s = line.strip()
        if s and not s.startswith("#"):
            return s.split()
    pexit("Unexpected end-of-file")


def main(argv):
    endian = ">"
    args = argv[1:]
    while args and args[0] in ("-b", "-l"):
        endian = ">" if args[0] == "-b" else "<"
        args = args[1:]
    if len(args) != 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    image_path, proto_path = args
    with open(proto_path) as f:
        lines = iter(f.read().split("\n"))

    next_line(lines)
    tokens = next_line(lines)
    blocks, inodes = int(tokens[0]), int(tokens[1])
    tokens = next_line(lines)
    fs = FileSystem(blocks, inodes, endian)
    root = fs.alloc_inode(mode_con(tokens[0]), int(tokens[1]), int(tokens[2]))
    fs.rootdir(root)
    fs.eat_dir(root, lines)

    with open(image_path, "wb") as f:
        f.write(fs.image)
    used = fs.next_zone - fs.firstdatazone
    print("%s: %d blocks, %d inodes; %d inodes and %d data zones used"
          % (image_path, blocks, inodes, fs.next_inode - 1, used))


if __name__ == "__main__":
    main(sys.argv)

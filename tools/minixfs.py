#!/usr/bin/env python3
"""
minixfs.py -- read and check a Minix V1 file system on the Mac.

USAGE
    python3 tools/minixfs.py IMAGE ls [PATH]        list a directory
    python3 tools/minixfs.py IMAGE get PATH [OUT]   copy a file out (stdout if no OUT)
    python3 tools/minixfs.py IMAGE check            consistency check (like fsck -n)

    IMAGE is either a raw Lisa ProFile image made by tools/mklisa.py (532-byte
    blocks: 20 tag bytes then 512 data bytes), in which case the file system
    area is found from the header in block 1, or a plain file system image
    (tools/mkfs.py output). Read only: IMAGE is never modified.
    --little   little-endian structures (default big-endian, 68000)

The check reports what fsck would: bad magic, zones or inodes used but not
marked in the bit maps (and marked but not used), zones claimed twice,
link counts that disagree with directory entries, directories without
"." or "..", and file sizes larger than their zones. Exit status 0 if
clean, 1 if not.

Layout is as in src/commands/mkfs.c and src/fs: 1024-byte blocks; block 1
super block (ninodes, nzones, imap_blocks, zmap_blocks, firstdatazone,
log_zone_size, max_size, magic 0x137F); inode map, zone map; 32-byte
inodes; bit n of a map is bit n % 16 of 16-bit word n / 16; zone map bit 0
stands for zone firstdatazone - 1.
"""

import struct
import sys

BLOCK = 1024
PROFILE_BLOCK = 532
TAG = 20
SUPER_MAGIC = 0x137F
INODE_SIZE = 32
DIR_SIZE = 16
NR_DZONE = 7
NR_INDIRECTS = BLOCK // 2
I_TYPE = 0o170000
I_DIRECTORY = 0o040000
I_REGULAR = 0o100000
I_BLOCK_SPECIAL = 0o060000
I_CHAR_SPECIAL = 0o020000
MNXL = 0x4D4E584C


def fail(msg):
    sys.stderr.write("minixfs: %s\n" % msg)
    sys.exit(2)


class FS:
    def __init__(self, path, endian=">"):
        self.e = endian
        with open(path, "rb") as f:
            raw = f.read()
        self.data = self.find_fs(raw)
        sb = struct.unpack_from(self.e + "HHHHHhIh", self.data, BLOCK)
        (self.ninodes, self.nzones, self.imap_blocks, self.zmap_blocks,
         self.firstdatazone, self.log_zone_size, self.max_size, magic) = sb
        if magic != SUPER_MAGIC:
            fail("bad super block magic 0x%04x" % (magic & 0xFFFF))
        if self.log_zone_size != 0:
            fail("zone size != block size is not supported")
        self.inode_start = 2 + self.imap_blocks + self.zmap_blocks

    def find_fs(self, raw):
        if len(raw) % PROFILE_BLOCK == 0 and len(raw) >= 2 * PROFILE_BLOCK:
            hdr = raw[PROFILE_BLOCK + TAG:PROFILE_BLOCK + TAG + 32]
            magic, _v, _fi, _ic, _load, _entry, fs_first, fs_count = struct.unpack(">8I", hdr)
            if magic == MNXL:
                if fs_count == 0:
                    fail("ProFile image has no file system area")
                out = bytearray()
                for b in range(fs_first, fs_first + fs_count):
                    pos = b * PROFILE_BLOCK + TAG
                    out += raw[pos:pos + 512]
                return bytes(out)
        return raw

    def block(self, n):
        return self.data[n * BLOCK:(n + 1) * BLOCK]

    def inode(self, n):
        pos = (self.inode_start * BLOCK) + (n - 1) * INODE_SIZE
        f = struct.unpack_from(self.e + "HHIIBB9H", self.data, pos)
        return {"mode": f[0], "uid": f[1], "size": f[2], "mtime": f[3],
                "gid": f[4], "nlinks": f[5], "zone": list(f[6:])}

    def zones(self, ino):
        """Data zones of a regular file or directory, in order; also the
        indirect zones (second list)."""
        z = [x for x in ino["zone"][:NR_DZONE]]
        indirect = []
        if ino["zone"][NR_DZONE]:
            indirect.append(ino["zone"][NR_DZONE])
            blk = self.block(ino["zone"][NR_DZONE])
            z += list(struct.unpack(self.e + "%dH" % NR_INDIRECTS, blk))
        if ino["zone"][NR_DZONE + 1]:
            dbl = ino["zone"][NR_DZONE + 1]
            indirect.append(dbl)
            for ind in struct.unpack(self.e + "%dH" % NR_INDIRECTS, self.block(dbl)):
                if ind:
                    indirect.append(ind)
                    z += list(struct.unpack(self.e + "%dH" % NR_INDIRECTS, self.block(ind)))
                else:
                    z += [0] * NR_INDIRECTS
        return z, indirect

    def read_file(self, ino):
        z, _ = self.zones(ino)
        out = bytearray()
        size = ino["size"]
        for zone in z:
            if len(out) >= size:
                break
            out += self.block(zone) if zone else bytes(BLOCK)
        return bytes(out[:size])

    def dir_entries(self, ino):
        data = self.read_file(ino)
        for pos in range(0, len(data) - DIR_SIZE + 1, DIR_SIZE):
            num, name = struct.unpack_from(self.e + "H14s", data, pos)
            if num:
                yield num, name.split(b"\0")[0].decode("latin-1")

    def lookup(self, path):
        n = 1
        for part in [p for p in path.split("/") if p]:
            ino = self.inode(n)
            if ino["mode"] & I_TYPE != I_DIRECTORY:
                fail("%s: not a directory" % part)
            for num, name in self.dir_entries(ino):
                if name == part:
                    n = num
                    break
            else:
                fail("%s: not found" % path)
        return n

    def bit(self, first_block, i):
        pos = first_block * BLOCK + 2 * (i // 16)
        return (struct.unpack_from(self.e + "H", self.data, pos)[0] >> (i % 16)) & 1


def cmd_ls(fs, path):
    n = fs.lookup(path)
    ino = fs.inode(n)
    if ino["mode"] & I_TYPE != I_DIRECTORY:
        entries = [(n, path)]
    else:
        entries = list(fs.dir_entries(ino))
    for num, name in entries:
        i = fs.inode(num)
        kind = {I_DIRECTORY: "d", I_REGULAR: "-", I_BLOCK_SPECIAL: "b",
                I_CHAR_SPECIAL: "c"}.get(i["mode"] & I_TYPE, "?")
        size = i["size"] if kind in "-d" else "%d,%d" % (i["zone"][0] >> 8, i["zone"][0] & 0xFF)
        print("%s%04o %2d %3d %3d %8s %s" % (kind, i["mode"] & 0o7777, i["nlinks"],
                                            i["uid"], i["gid"], size, name))


def cmd_check(fs):
    problems = []
    zmap = 2 + fs.imap_blocks
    used_inodes = {}
    zone_owner = {}
    links = {}

    def claim(z, who):
        if z == 0:
            return
        if z < fs.firstdatazone or z >= fs.nzones:
            problems.append("inode %d: zone %d out of range" % (who, z))
            return
        if z in zone_owner:
            problems.append("zone %d claimed by inodes %d and %d" % (z, zone_owner[z], who))
        zone_owner[z] = who

    # Walk the tree from the root, counting directory entries.
    stack = [(1, 1)]
    seen = set()
    links[1] = 1          # the root's own ".." counted below; root has no parent entry
    while stack:
        n, parent = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        ino = fs.inode(n)
        used_inodes[n] = ino
        kind = ino["mode"] & I_TYPE
        if kind in (I_REGULAR, I_DIRECTORY):
            z, ind = fs.zones(ino)
            nz = (ino["size"] + BLOCK - 1) // BLOCK
            for zone in z[:nz]:
                claim(zone, n)
            for zone in z[nz:]:
                if zone:
                    problems.append("inode %d: zone %d beyond file size" % (n, zone))
                    claim(zone, n)
            for zone in ind:
                claim(zone, n)
        if kind == I_DIRECTORY:
            names = {}
            for num, name in fs.dir_entries(ino):
                if num > fs.ninodes:
                    problems.append("dir inode %d: entry %s has bad inode %d" % (n, name, num))
                    continue
                names[name] = num
                links[num] = links.get(num, 0) + 1
                if name not in (".", ".."):
                    stack.append((num, n))
            if names.get(".") != n:
                problems.append("dir inode %d: bad or missing '.'" % n)
            if names.get("..") != parent:
                problems.append("dir inode %d: '..' is %s, expected %d" % (n, names.get(".."), parent))
    links[1] -= 1

    for n, ino in used_inodes.items():
        if ino["nlinks"] != links.get(n, 0):
            problems.append("inode %d: link count %d, %d entries" % (n, ino["nlinks"], links.get(n, 0)))
        if not fs.bit(2, n):
            problems.append("inode %d in use but not in the inode map" % n)
    for n in range(1, fs.ninodes + 1):
        if fs.bit(2, n) and n not in used_inodes:
            problems.append("inode %d marked in the map but not referenced" % n)
    for z in range(fs.firstdatazone, fs.nzones):
        bit = fs.bit(zmap, z - (fs.firstdatazone - 1))
        if z in zone_owner and not bit:
            problems.append("zone %d in use but not in the zone map" % z)
        if bit and z not in zone_owner:
            problems.append("zone %d marked in the map but not in use" % z)

    free_z = sum(1 for z in range(fs.firstdatazone, fs.nzones) if z not in zone_owner)
    print("%d inodes used of %d, %d zones used, %d free" %
          (len(used_inodes), fs.ninodes, len(zone_owner), free_z))
    for p in problems[:50]:
        print("  " + p)
    if len(problems) > 50:
        print("  ... %d more" % (len(problems) - 50))
    print("clean" if not problems else "%d problems" % len(problems))
    return 0 if not problems else 1


def main(argv):
    args = argv[1:]
    endian = ">"
    if "--little" in args:
        args.remove("--little")
        endian = "<"
    if len(args) < 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    fs = FS(args[0], endian)
    cmd = args[1]
    if cmd == "ls":
        cmd_ls(fs, args[2] if len(args) > 2 else "/")
    elif cmd == "get":
        if len(args) < 3:
            fail("get needs a path")
        data = fs.read_file(fs.inode(fs.lookup(args[2])))
        if len(args) > 3:
            with open(args[3], "wb") as f:
                f.write(data)
        else:
            sys.stdout.buffer.write(data)
    elif cmd == "check":
        sys.exit(cmd_check(fs))
    else:
        fail("unknown command %s" % cmd)


if __name__ == "__main__":
    main(sys.argv)

#!/usr/bin/env python3
"""
mkdc42.py -- make and read Lisa 400K floppy images in DiskCopy 4.2 format.

USAGE
    python3 tools/mkdc42.py create IMAGE [DATA]   400K image; data from DATA
                                                  (at most 409600 bytes,
                                                  zero-padded), else zeros
    python3 tools/mkdc42.py data IMAGE OUT        copy the 409600 data bytes out
    python3 tools/mkdc42.py check IMAGE           verify header and checksums

A Sony 400K disk has 800 blocks of 512 data bytes and 12 tag bytes. Blocks
are stored in Lisa block order (LisaEm lisa/io_board/floppy.c), so a Minix
file system made by tools/mkfs.py with 400 1024-byte blocks is exactly the
data area. Tags are written as zeros.

DC42 layout (LisaEm src/lib/libdc42/src/libdc42.c dc42_create and the
checksum functions), big-endian:
    0   64 bytes  volume name, Pascal string
    64  long      data size (409600)
    68  long      tag size (9600)
    72  long      data checksum: for each 16-bit word w of the data,
                  c = rotate_right_1(c + w)
    76  long      tag checksum: the same over the tags of blocks 1-799
    80  byte      disk encoding 0x00 (Sony 400K)
    81  byte      format 0x02 (Sony 400K)
    82  word      0x0100
    84            data, then tags
"""

import struct
import sys

BLOCKS = 800
DATA = BLOCKS * 512
TAGS = BLOCKS * 12
HEADER = 84


def checksum(data):
    c = 0
    for (w,) in struct.iter_unpack(">H", data):
        c = (c + w) & 0xFFFFFFFF
        c = ((c >> 1) | ((c & 1) << 31)) & 0xFFFFFFFF
    return c


def create(path, data, name=b"Minix"):
    if len(data) > DATA:
        sys.exit("mkdc42: data is %d bytes, more than %d" % (len(data), DATA))
    data = data + bytes(DATA - len(data))
    tags = bytes(TAGS)
    vol = bytes([len(name)]) + name + bytes(63 - len(name))
    hdr = vol + struct.pack(">IIIIBBH", DATA, TAGS, checksum(data),
                            checksum(tags[12:]), 0x00, 0x02, 0x0100)
    assert len(hdr) == HEADER
    with open(path, "wb") as f:
        f.write(hdr + data + tags)


def read(path):
    with open(path, "rb") as f:
        img = f.read()
    dsize, tsize, dsum, tsum, enc, fmt, magic = struct.unpack_from(">IIIIBBH", img, 64)
    if dsize != DATA or tsize != TAGS or len(img) != HEADER + DATA + TAGS:
        sys.exit("mkdc42: %s is not a 400K DC42 image" % path)
    data = img[HEADER:HEADER + DATA]
    tags = img[HEADER + DATA:]
    return data, tags, dsum, tsum, enc, fmt, magic


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        sys.exit(2)
    cmd, path = argv[1], argv[2]
    if cmd == "create":
        data = b""
        if len(argv) > 3:
            with open(argv[3], "rb") as f:
                data = f.read()
        create(path, data)
    elif cmd == "data":
        data = read(path)[0]
        with open(argv[3], "wb") as f:
            f.write(data)
    elif cmd == "check":
        data, tags, dsum, tsum, enc, fmt, magic = read(path)
        ok = (dsum == checksum(data) and tsum == checksum(tags[12:]) and
              enc == 0 and fmt == 2 and magic == 0x0100)
        print("%s: data checksum %08x (computed %08x), tag checksum %08x (computed %08x), %s"
              % (path, dsum, checksum(data), tsum, checksum(tags[12:]),
                 "ok" if ok else "BAD"))
        sys.exit(0 if ok else 1)
    else:
        sys.exit("mkdc42: unknown command %s" % cmd)


if __name__ == "__main__":
    main(sys.argv)

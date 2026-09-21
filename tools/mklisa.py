#!/usr/bin/env python3
"""
mklisa.py -- build a bootable raw ProFile image for Minix on the Lisa.

USAGE
    python3 tools/mklisa.py [options] BOOTBLOCK IMAGE OUTPUT

    BOOTBLOCK       boot block binary (boot/lisaboot.S), at most 512 bytes
    IMAGE           raw binary loaded by the boot block (for example an
                    objcopy -O binary of a kernel linked at --load)
    OUTPUT          raw .image file to create; refuses to overwrite unless -f

    --load ADDR     logical load address of IMAGE (default 0)
    --entry ADDR    entry point (default: the load address)
    --fs FILE       Minix file system to place after the image
    --size 5|10     ProFile size in MB (default 5)
    -f              overwrite OUTPUT

LAYOUT (PLAN.md decision 3)
    The file is a sequence of 532-byte blocks, 20 tag bytes then 512 data
    bytes, block N at offset N * 532, as LisaEm and ESProFile read raw
    images. Its size is exactly 5,175,296 bytes (9728 blocks) or 10,350,592
    bytes (19456 blocks).

    block 0     tag bytes 4-5 = 0xAAAA (the boot ROM's check), data = boot
                block
    block 1     header, big-endian longs: magic "MNXL", version 1, first
                image block, image block count, load address, entry address,
                first file system block, file system block count (0 if none)
    block 2...  IMAGE, padded to whole blocks
    then        the file system, if any, starting at the next block

    All other tag bytes are zero.
"""

import argparse
import os
import struct
import sys

BLOCK = 512
TAG = 20
SIZES = {5: 9728, 10: 19456}
MAGIC = 0x4D4E584C
VERSION = 1


def fail(msg):
    sys.stderr.write("mklisa: %s\n" % msg)
    sys.exit(1)


def blocks(data):
    return (len(data) + BLOCK - 1) // BLOCK


def main():
    ap = argparse.ArgumentParser(description="Build a bootable Lisa ProFile image.")
    ap.add_argument("bootblock")
    ap.add_argument("image")
    ap.add_argument("output")
    ap.add_argument("--load", default="0")
    ap.add_argument("--entry")
    ap.add_argument("--fs")
    ap.add_argument("--size", type=int, choices=sorted(SIZES), default=5)
    ap.add_argument("-f", action="store_true", dest="force")
    args = ap.parse_args()

    if os.path.exists(args.output) and not args.force:
        fail("%s exists (use -f to overwrite)" % args.output)

    boot = open(args.bootblock, "rb").read()
    if len(boot) > BLOCK:
        fail("boot block is %d bytes, more than %d" % (len(boot), BLOCK))
    image = open(args.image, "rb").read()
    if not image:
        fail("%s is empty" % args.image)
    fs = open(args.fs, "rb").read() if args.fs else b""

    load = int(args.load, 0)
    entry = int(args.entry, 0) if args.entry else load
    nblocks = SIZES[args.size]

    first_image = 2
    image_blocks = blocks(image)
    first_fs = first_image + image_blocks if fs else 0
    fs_blocks = blocks(fs)
    used = first_image + image_blocks + fs_blocks
    if used > nblocks:
        fail("needs %d blocks, a %d MB ProFile has %d" % (used, args.size, nblocks))

    out = bytearray(nblocks * (TAG + BLOCK))

    def put(n, data, tag=bytes(TAG)):
        pos = n * (TAG + BLOCK)
        out[pos:pos + TAG] = tag
        out[pos + TAG:pos + TAG + len(data)] = data

    tag0 = bytearray(TAG)
    tag0[4:6] = b"\xAA\xAA"
    put(0, boot, bytes(tag0))
    put(1, struct.pack(">8I", MAGIC, VERSION, first_image, image_blocks,
                       load, entry, first_fs, fs_blocks))
    for i in range(image_blocks):
        put(first_image + i, image[i * BLOCK:(i + 1) * BLOCK])
    for i in range(fs_blocks):
        put(first_fs + i, fs[i * BLOCK:(i + 1) * BLOCK])

    with open(args.output, "wb") as f:
        f.write(out)
    print("%s: %d MB, image %d blocks at block %d (load %#x, entry %#x)%s"
          % (args.output, args.size, image_blocks, first_image, load, entry,
             ", file system %d blocks at block %d" % (fs_blocks, first_fs)
             if fs else ""))


if __name__ == "__main__":
    main()

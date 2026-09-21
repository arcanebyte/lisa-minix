#!/usr/bin/env python3
"""
lisascreen.py -- read the text on the Lisa screen from a LisaEm screen dump.

USAGE
    python3 tools/lisascreen.py SCREEN.png        print the 40 text rows
    python3 tools/lisascreen.py SCREEN.png --raw  also mark unknown cells '?'

SCREEN.png is the file LisaEm's LISAEM_SCREEN_DUMP writes (720 x 364, black
and white, 8-bit RGB PNG). The Minix console (src/kernel/lisa/lisavdu.c)
draws 90 x 40 character cells of 8 x 9 pixels from the 8 x 8 font in
src/kernel/stfnt.c plus a blank line, black on white, reverse video for the
cursor and highlighted text. Each cell is matched exactly against the font,
normal or inverted; trailing blanks are stripped. A cell that matches
nothing prints as a space, or '?' with --raw.

The PNG reader handles only what LisaEm writes: non-interlaced, bit depth 8,
colour type 2 (RGB) or 0 (grey), all five filter types.
"""

import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_SRC = os.path.join(os.path.dirname(HERE), "src/kernel/stfnt.c")
COLS, ROWS, CW, CH = 90, 40, 8, 9


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("%s: not a PNG" % path)
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _c, _f, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    if depth != 8 or ctype not in (0, 2) or interlace:
        sys.exit("%s: unsupported PNG format" % path)
    bpp = 3 if ctype == 2 else 1
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        ftype = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 255
            elif ftype == 2:
                line[x] = (line[x] + b) & 255
            elif ftype == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        rows.append([line[x * bpp] < 128 for x in range(w)])   # True = black
        prev = line
    return w, h, rows


def load_font():
    with open(FONT_SRC) as f:
        src = f.read()
    body = src[src.index("font8[]"):]
    body = body[body.index("{") + 1:body.index("};")]
    vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", body)]
    glyphs = {}
    for code in range(0x20, 0x7F):
        rows = tuple(vals[code * 8:code * 8 + 8]) + (0,)
        glyphs.setdefault(rows, chr(code))
        inverted = tuple(~r & 0xFF for r in rows)
        glyphs.setdefault(inverted, chr(code))
    return glyphs


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    raw = "--raw" in argv
    if len(args) != 1:
        sys.stderr.write(__doc__)
        sys.exit(2)
    w, h, px = read_png(args[0])
    if (w, h) != (720, 364):
        sys.exit("expected a 720 x 364 screen, got %d x %d" % (w, h))
    glyphs = load_font()
    for r in range(ROWS):
        text = ""
        for c in range(COLS):
            cell = []
            for y in range(CH):
                bits = 0
                row = px[r * CH + y]
                for x in range(CW):
                    if row[c * CW + x]:
                        bits |= 0x80 >> x
                cell.append(bits)
            text += glyphs.get(tuple(cell), "?" if raw else " ")
        print(text.rstrip())


if __name__ == "__main__":
    main(sys.argv)

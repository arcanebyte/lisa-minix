#!/usr/bin/env python3
"""
elf2mnx.py -- convert an m68k ELF executable into a Minix-ST executable.

Replaces the ACK tool commands/atari/cv.c for gcc builds. Needs only
Python 3.

USAGE
    python3 tools/elf2mnx.py [-R] [+N|-N|=N] INPUT.elf OUTPUT

    -R      write an empty relocation list (as cv -R; used for the kernel)
    +N -N   add or remove N bytes from the default stack+malloc area
    =N      set the stack+malloc area to N bytes

INPUT must be linked at address 0 with toolchain/minix.ld and with
relocations kept (ld -q / --emit-relocs), so that every absolute
reference can be found.

OUTPUT FORMAT (src/mm/exec.c read_header and relocate, src/tools/build.c)
    header, 8 big-endian longs:
        0x04100301 (combined I & D), 0x00000020, text size, data size,
        bss size, 0 (entry point), total memory, symbol table size (0)
    text (.text and read-only data, up to the start of .data)
    data (up to the start of .bss)
    relocation in GEMDOS format: a long giving the offset of the first
        long to relocate (0 = none), then one byte per further one: an
        even value is the distance to it, 1 advances 254 bytes, 0 ends.

    The default stack+malloc area follows cv.c: 64 KB minus text, data
    and bss, plus 64 KB until it is positive.

Relocations are taken from the ELF .rela sections that apply to text and
data. PC-relative relocations need nothing. References to absolute
symbols (SHN_ABS, such as I/O addresses) are not relocated. Any other
relocation that is not a 32-bit absolute one (R_68K_32) cannot be
expressed in the GEMDOS format and is an error, as is an odd address or
a relocation at offset 0 (which the format cannot represent).
"""

import struct
import sys

EM_68K = 4
ET_EXEC = 2
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8
SHF_ALLOC = 0x2
SHN_UNDEF = 0
SHN_ABS = 0xFFF1

R_68K_NONE = 0
R_68K_32 = 1
R_68K_16 = 2
R_68K_8 = 3
R_68K_PC32 = 4
R_68K_PC16 = 5
R_68K_PC8 = 6
PC_RELATIVE = {R_68K_PC32, R_68K_PC16, R_68K_PC8}

MAGIC_COMBINED = 0x04100301
HEADER_VERSION = 0x00000020


def fail(msg):
    sys.stderr.write("elf2mnx: %s\n" % msg)
    sys.exit(1)


class Section:
    pass


def read_elf(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        fail("%s: not an ELF file" % path)
    if data[4] != 1 or data[5] != 2:
        fail("%s: not 32-bit big-endian ELF" % path)
    (e_type, e_machine, _ver, e_entry, _phoff, e_shoff, _flags, _ehsize,
     _phentsize, _phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(
        ">HHIIIIIHHHHHH", data, 16)
    if e_machine != EM_68K:
        fail("%s: not an m68k file" % path)
    if e_type != ET_EXEC:
        fail("%s: not a linked executable" % path)
    if e_entry != 0:
        fail("%s: entry point is 0x%x, must be 0" % (path, e_entry))

    sections = []
    for i in range(e_shnum):
        s = Section()
        (s.name_off, s.type, s.flags, s.addr, s.offset, s.size, s.link,
         s.info, s.addralign, s.entsize) = struct.unpack_from(
            ">IIIIIIIIII", data, e_shoff + i * e_shentsize)
        s.index = i
        sections.append(s)
    strtab = sections[e_shstrndx]
    for s in sections:
        end = data.index(b"\0", strtab.offset + s.name_off)
        s.name = data[strtab.offset + s.name_off:end].decode()
    return data, sections


def symbols(data, symtab):
    syms = []
    for i in range(symtab.size // 16):
        _name, value, _size, _info, _other, shndx = struct.unpack_from(
            ">IIIBBH", data, symtab.offset + i * 16)
        syms.append((value, shndx))
    return syms


def main(argv):
    rflag = False
    chmem = None
    args = argv[1:]
    while args and args[0][:1] in "-+=" and len(args[0]) > 1:
        if args[0] == "-R":
            rflag = True
        else:
            try:
                chmem = (args[0][0], int(args[0][1:], 0))
            except ValueError:
                fail("bad chmem amount %s" % args[0])
        args = args[1:]
    if len(args) != 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    inpath, outpath = args

    data, sections = read_elf(inpath)
    alloc = [s for s in sections if s.flags & SHF_ALLOC and s.size > 0]
    alloc.sort(key=lambda s: s.addr)
    if not alloc or alloc[0].addr != 0:
        fail("first section must start at address 0")

    def find(name):
        for s in sections:
            if s.name == name:
                return s
        return None

    data_sec = find(".data")
    bss_sec = find(".bss")
    image_end = max(s.addr + s.size for s in alloc)
    data_start = data_sec.addr if data_sec else None
    bss_start = bss_sec.addr if bss_sec and bss_sec.size else image_end
    if data_start is None:
        data_start = bss_start

    # Everything loaded from the file must come before .bss, and .bss must
    # be the only NOBITS section, at the end.
    for s in alloc:
        if s.type == SHT_NOBITS:
            if s is not bss_sec:
                fail("unexpected no-bits section %s" % s.name)
        elif s.addr + s.size > bss_start:
            fail("section %s overlaps .bss" % s.name)
    if data_start > bss_start:
        fail(".data must come before .bss")

    text_size = data_start
    data_size = bss_start - data_start
    bss_size = image_end - bss_start

    image = bytearray(bss_start)
    for s in alloc:
        if s.type != SHT_NOBITS:
            image[s.addr:s.addr + s.size] = data[s.offset:s.offset + s.size]

    relocs = set()
    if not rflag:
        loaded = {s.index for s in alloc if s.type != SHT_NOBITS}
        rela_found = False
        for rs in sections:
            if rs.type != SHT_RELA or rs.info not in loaded:
                continue
            rela_found = True
            syms = symbols(data, sections[rs.link])
            for i in range(rs.size // 12):
                r_offset, r_info, _addend = struct.unpack_from(
                    ">IIi", data, rs.offset + i * 12)
                rtype = r_info & 0xFF
                symndx = r_info >> 8
                _value, shndx = syms[symndx]
                if rtype == R_68K_NONE or rtype in PC_RELATIVE:
                    continue
                # Symbol 0 or an absolute symbol: the value is a constant.
                if symndx == 0 or shndx == SHN_ABS:
                    continue
                if shndx == SHN_UNDEF:
                    fail("undefined symbol referenced at 0x%x" % r_offset)
                if rtype != R_68K_32:
                    fail("relocation type %d at 0x%x cannot be expressed "
                         "(only 32-bit absolute references can be relocated)"
                         % (rtype, r_offset))
                if r_offset & 1:
                    fail("relocation at odd address 0x%x" % r_offset)
                if r_offset == 0:
                    fail("relocation at offset 0 cannot be expressed")
                if r_offset + 4 > bss_start:
                    fail("relocation at 0x%x is outside text and data" % r_offset)
                if r_offset in relocs:
                    fail("two relocations at 0x%x" % r_offset)
                relocs.add(r_offset)
        if not rela_found and any(s.type != SHT_NOBITS for s in alloc):
            fail("no relocation sections: link with -q (--emit-relocs)")

    rel = bytearray()
    last = 0
    for curr in sorted(relocs):
        if last == 0:
            rel += struct.pack(">I", curr)
        else:
            while curr - last > 254:
                rel.append(1)
                last += 254
            rel.append(curr - last)
        last = curr
    if last == 0:
        rel += struct.pack(">I", 0)
    else:
        rel.append(0)

    stack = 0x10000 - (data_size + bss_size) - text_size
    while stack < 0:
        stack += 0x10000
    if chmem:
        op, num = chmem
        stack = {"-": stack - num, "+": stack + num, "=": num}[op]
    if stack <= 0:
        fail("stack+malloc area must be positive")
    total = stack + data_size + bss_size + text_size

    header = struct.pack(">8I", MAGIC_COMBINED, HEADER_VERSION, text_size,
                         data_size, bss_size, 0, total, 0)
    with open(outpath, "wb") as f:
        f.write(header)
        f.write(image)
        f.write(rel)
    sys.stderr.write("%s: text %d data %d bss %d, %d bytes assigned to "
                     "stack+malloc area, %d relocations\n"
                     % (outpath, text_size, data_size, bss_size, stack,
                        len(relocs)))


if __name__ == "__main__":
    main(sys.argv)

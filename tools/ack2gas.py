#!/usr/bin/env python3
"""
ack2gas.py -- mechanical conversion of Minix-ST ACK 68000 assembly to GNU as.

USAGE
    python3 tools/ack2gas.py FILE.s [FILE.s ...]     (converts in place)

The output is assembled with
    m68k-elf-gcc -c -x assembler-with-cpp -m68000 -Wa,--register-prefix-optional

so the Motorola operand syntax (move.l 4(sp),a0) is kept as it is. This
tool only does what is mechanical; every converted file still needs a
review for the things it cannot know (see docs/toolchain.md):
index register sizes, branch sizes where offsets matter, and registers
that gcc expects to be preserved (d2-d7/a2-a6).

Changes made:
    ! comment               | comment       (outside quoted strings)
    .define a, b            .globl a, b
    .extern a               (removed; gas treats undefined symbols as extern)
    .sect .text/.data/.bss  .text/.data/.bss
    .sect .rom              .section .rodata
    .sect .end              (removed; end is defined by toolchain/minix.ld)
    .data1/.data2/.data4    .byte/.word/.long
    _name                   name   (ACK prefixed C names with '_'; m68k ELF
                                    does not, so one leading '_' is dropped)
    #endif ACK              #endif /* ACK */   (and the same for #else)
"""

import re
import sys

IDENT = re.compile(r"(?<![\w.$])_([A-Za-z_]\w*)")


def split_comment(line):
    """Split at the first '!' that is not inside a quoted string."""
    # ACK code uses only double-quoted strings; apostrophes appear only in
    # comments, which start before them.
    in_string = False
    for i, c in enumerate(line):
        if c == '"':
            in_string = not in_string
        elif c == "!" and not in_string:
            return line[:i], line[i + 1:]
    return line, None


def rename(code):
    """Drop one leading underscore from identifiers outside strings."""
    out = []
    for i, part in enumerate(re.split(r'("(?:[^"\\]|\\.)*")', code)):
        out.append(part if i % 2 else IDENT.sub(r"\1", part))
    return "".join(out)


def convert_line(line):
    stripped = line.lstrip()
    if stripped.startswith("#"):
        m = re.match(r"(\s*#\s*(?:endif|else))\s+([A-Za-z_]\w*)\s*$", line)
        if m:
            return "%s /* %s */" % (m.group(1), m.group(2))
        return line

    code, comment = split_comment(line)
    if comment is not None:
        comment = "|" + comment

    m = re.match(r"(\s*)\.define\s+(.*?)\s*$", code)
    if m:
        code = "%s.globl\t%s" % (m.group(1), rename(m.group(2)))
    elif re.match(r"\s*\.extern\b", code):
        code = ""
        if comment is None:
            return None
    else:
        m = re.match(r"(\s*)\.sect\s+\.(\w+)\s*$", code)
        if m:
            sect = m.group(2)
            if sect in ("text", "data", "bss"):
                code = "%s.%s" % (m.group(1), sect)
            elif sect == "rom":
                code = "%s.section\t.rodata" % m.group(1)
            elif sect == "end":
                code = ""
                if comment is None:
                    return None
            else:
                raise SystemExit("unknown section .%s" % sect)
        else:
            code = re.sub(r"\.data1\b", ".byte", code)
            code = re.sub(r"\.data2\b", ".word", code)
            code = re.sub(r"\.data4\b", ".long", code)
            code = rename(code)

    if comment is None:
        return code
    if code.strip() == "":
        return code + comment
    return code + comment


def convert(path):
    with open(path) as f:
        lines = f.read().split("\n")
    out = []
    for line in lines:
        new = convert_line(line)
        if new is not None:
            out.append(new)
    with open(path, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        convert(p)

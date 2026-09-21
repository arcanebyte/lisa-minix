# Build Minix-ST 1.5 with the cross toolchain (docs/toolchain.md).
#
#   make            build/atari/minix.img, the boot image, and
#                   build/atari/minix_fd.st, the same padded to a floppy,
#                   and build/atari/root_fd.st, the root file system floppy
#   make hatari-boot  boot minix_fd.st in Hatari, screenshot in build/hatari
#   make hatari-shell boot with the root floppy and run a shell (phase 0 test)
#   make lisa-minix  build/lisa-minix/minix.image: Minix for the Lisa
#                   (MACHINE=LISA), a bootable raw ProFile image
#   make lisaem-minix  boot it in LisaEm (phase 2 test)
#   make lisa-minix-hd  build/lisa-minix/minix-hd.image: 10 MB ProFile image
#                   with the root file system on /dev/hd0
#   make lisaem-hd  phase 3 test: two boots, fsck, read back on the Mac
#   make clean      remove build/
#
# Mirrors src/{lib,kernel,mm,fs,tools}/Makefile, with ACK's cc, ld and cv
# replaced by m68k-minix-gcc, m68k-elf-ld and tools/elf2mnx.py, and
# tools/build.c by tools/build.py.

GCCPREFIX ?= $(HOME)/opt/m68k-minix/bin/m68k-minix-
BINUTILS  ?= m68k-elf-
PYTHON    ?= python3

CC      = $(GCCPREFIX)gcc
LD      = $(BINUTILS)ld
AR      = $(BINUTILS)ar
RANLIB  = $(BINUTILS)ranlib
OBJCOPY = $(BINUTILS)objcopy

SRC = src
# MACH=ATARI builds Minix-ST into build/atari; MACH=LISA builds Minix for
# the Lisa into build/lisa-minix (make lisa-minix).
MACH ?= ATARI
ifeq ($(MACH),LISA)
B   = build/lisa-minix
MACHDEF = -DMACHINE=LISA
else
B   = build/atari
MACHDEF =
endif

CFLAGS  = -mcpu=68000 -mshort -O -std=gnu89 -D_MINIX_KR $(MACHDEF) \
	  -fno-builtin -fno-tree-loop-distribute-patterns -w \
	  -nostdinc -I$(SRC)/include -MMD -MP
ASFLAGS = -mcpu=68000 -x assembler-with-cpp -Wa,--register-prefix-optional \
	  -DACK $(MACHDEF) -nostdinc -I$(SRC)/include
LDFLAGS = -q --no-warn-rwx-segments -e 0 -T toolchain/minix.ld
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

ELF2MNX = $(PYTHON) tools/elf2mnx.py

all: $(B)/minix.img $(B)/minix_fd.st $(B)/root_fd.st

# ---- library ---------------------------------------------------------------

LIB_ANSI  = $(patsubst $(SRC)/%.c,$(B)/%.o,$(wildcard $(SRC)/lib/ansi/*.c))
LIB_POSIX = $(patsubst $(SRC)/%.c,$(B)/%.o,$(wildcard $(SRC)/lib/posix/*.c))
LIB_OTHER = $(patsubst $(SRC)/%.c,$(B)/%.o,$(wildcard $(SRC)/lib/other/*.c))
# Only the Minix run-time; the _*.s files are ACK EM helpers gcc does not use.
LIB_ASM   = $(patsubst %,$(B)/lib/atari/%.o,brksize catchsig sendrec setjmp)
LIB_OBJ   = $(LIB_ANSI) $(LIB_POSIX) $(LIB_OTHER) $(LIB_ASM)

$(B)/lib/ansi/%.o $(B)/lib/posix/%.o: DEFS = -D_MINIX -D_POSIX_SOURCE

$(B)/libc.a: $(LIB_OBJ)
	rm -f $@
	$(AR) rc $@ $(LIB_OBJ)
	$(RANLIB) $@

HEAD  = $(B)/lib/atari/head.o
CRTSO = $(B)/lib/atari/crtso.o

# ---- kernel ----------------------------------------------------------------

KEYMAP = keymap.us.h
ifeq ($(MACH),LISA)
KERNEL_SOBJ = lisa/lisampx copy68k
KERNEL_COBJ = lisa/lisamain lisa/lisammu lisa/lisacons lisa/lisakbd lisa/lisavdu lisa/lisapro \
	      lisa/lisafloppy \
	      stfnt proc system stshadow \
	      tty clock memory table stdmp misc
KERNEL_LDFLAGS = -q --no-warn-rwx-segments -e 0 -T toolchain/lisa-kernel.ld
else
KERNEL_SOBJ = stmpx copy68k stdskclks
KERNEL_COBJ = stmain proc system stshadow tty clock memory stdma stfloppy \
	      stwini stcon stkbd stvdu stfnt stprint rs232 table stdmp misc \
	      stdskclk
KERNEL_LDFLAGS = $(LDFLAGS)
endif
# stmpx.o / lisampx.o first: it holds the exception vectors at address 0.
KERNEL_OBJ = $(patsubst %,$(B)/kernel/%.o,$(KERNEL_SOBJ) $(KERNEL_COBJ))

$(B)/kernel/%.o: DEFS = -DACK -I$(B)/kernel -I$(SRC)/kernel

$(B)/kernel/keymap.h: $(SRC)/kernel/$(KEYMAP)
	@mkdir -p $(@D)
	cp $< $@

$(patsubst %,$(B)/kernel/%.o,$(KERNEL_COBJ)): $(B)/kernel/keymap.h

$(B)/kernel.mix: $(KERNEL_OBJ) $(B)/libc.a
	$(LD) $(KERNEL_LDFLAGS) -o $(B)/kernel.elf $(KERNEL_OBJ) $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) -R $(B)/kernel.elf $@

# ---- mm, fs, init, menu ----------------------------------------------------

MM_OBJ = $(patsubst %,$(B)/mm/%.o,main forkexit break exec signal getset \
	 alloc utility table putc trace)
FS_OBJ = $(patsubst $(SRC)/%.c,$(B)/%.o,$(wildcard $(SRC)/fs/*.c))

$(B)/mm/%.o: DEFS = -I$(SRC)/mm
$(B)/fs/%.o: DEFS = -I$(SRC)/fs

$(B)/mm.mix: $(HEAD) $(MM_OBJ) $(B)/libc.a
	$(LD) $(LDFLAGS) -o $(B)/mm.elf $(HEAD) $(MM_OBJ) $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) $(B)/mm.elf $@

$(B)/fs.mix: $(HEAD) $(FS_OBJ) $(B)/libc.a
	$(LD) $(LDFLAGS) -o $(B)/fs.elf $(HEAD) $(FS_OBJ) $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) $(B)/fs.elf $@

$(B)/%.mix: $(HEAD) $(B)/tools/%.o $(B)/libc.a
	$(LD) $(LDFLAGS) -o $(B)/$*.elf $(HEAD) $(B)/tools/$*.o $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) $(B)/$*.elf $@

# ---- commands --------------------------------------------------------------

CMD_SIMPLE = login ls cat echo pwd mkdir rm cp sync
SH_OBJ     = $(patsubst %,$(B)/commands/sh/%.o,sh1 sh2 sh3 sh4 sh5 sh6)
CMDS       = $(patsubst %,$(B)/cmd/%,sh $(CMD_SIMPLE))

$(B)/commands/%.o: DEFS = -D_MINIX -D_POSIX_SOURCE

$(B)/cmd/sh: $(CRTSO) $(SH_OBJ) $(B)/libc.a
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@.elf $(CRTSO) $(SH_OBJ) $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) $@.elf $@

$(B)/cmd/%: $(CRTSO) $(B)/commands/%.o $(B)/libc.a
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@.elf $(CRTSO) $(B)/commands/$*.o $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) $@.elf $@

# ---- root file system ------------------------------------------------------

# Non-boot sector with the BPB of a single-sided 80-track floppy
# (src/tools/Makefile type_fd), so TOS and Hatari know the geometry.
$(B)/type_fd: $(SRC)/tools/type.s
	@mkdir -p $(@D)
	$(CC) -c $(ASFLAGS) -Dtype_fd $< -o $@.o
	$(OBJCOPY) -O binary -j .text $@.o $@

# Root floppy: a 360-block Minix file system padded to a 720-sector floppy,
# with the type_fd BPB in the unused start of block 0.
$(B)/root_fd.st: rootfs/atari.proto $(CMDS) $(wildcard rootfs/etc/*) tools/mkfs.py $(B)/type_fd
	$(PYTHON) tools/mkfs.py $(B)/root.fs rootfs/atari.proto
	$(PYTHON) -c 'import sys; d = bytearray(open(sys.argv[1], "rb").read()); \
		t = open(sys.argv[2], "rb").read(); d[:len(t)] = t; \
		open(sys.argv[3], "wb").write(d + bytes(368640 - len(d)))' \
		$(B)/root.fs $(B)/type_fd $@

# ---- boot block and image ---------------------------------------------------

$(B)/boot_fd.o: $(SRC)/tools/boot.s $(SRC)/tools/type.s
	@mkdir -p $(@D)
	$(CC) -c $(ASFLAGS) -Dboot_fd -I$(SRC)/tools $< -o $@

$(B)/boot_fd: $(B)/boot_fd.o
	$(OBJCOPY) -O binary -j .text $< $@

PARTS = $(B)/kernel.mix $(B)/mm.mix $(B)/fs.mix $(B)/init.mix $(B)/menu.mix

$(B)/minix.img: $(B)/boot_fd $(PARTS) tools/build.py
	$(PYTHON) tools/build.py $(B)/boot_fd $(PARTS) $@

# The image padded to a single-sided 80-track, 9-sector floppy (720
# sectors, as the boot sector's BPB says), for Hatari.
$(B)/minix_fd.st: $(B)/minix.img
	$(PYTHON) -c 'import sys; d = open(sys.argv[1], "rb").read(); \
		assert len(d) <= 368640, "image larger than a floppy"; \
		open(sys.argv[2], "wb").write(d + bytes(368640 - len(d)))' $< $@

hatari-boot: $(B)/minix_fd.st
	$(PYTHON) tools/hatari_run.py $< wait:15 shot:boot

# Phase 0 exit test: boot, swap in the root floppy, log in as root and run
# commands; screenshot build/hatari/shell.png.
hatari-shell: $(B)/minix_fd.st $(B)/root_fd.st
	$(PYTHON) tools/hatari_run.py $(B)/minix_fd.st \
		wait:12 disk:$(B)/root_fd.st wait:2 key:28 wait:30 \
		type:root key:28 wait:8 \
		type:ls key:57 type:-l key:57 type:/bin key:28 wait:5 \
		type:cat key:57 type:/etc/passwd key:28 wait:5 shot:shell

# ---- Minix for the Lisa (MACH=LISA) ------------------------------------------

lisa-minix:
	$(MAKE) MACH=LISA build/lisa-minix/minix.image

# Phase 2 exit test: boot, log in as root, list / and run cat through the
# shell (fork and exec).
lisaem-minix: lisa-minix
	$(PYTHON) tools/lisaem_run.py build/lisa-minix/minix.image --timeout 240 \
		--send 'login:=root' --send '# $$=ls -l /' \
		--send '# $$=cat /etc/passwd' --until 'Andy Tanenbaum.*\n# $$'

lisa-minix-hd:
	$(MAKE) MACH=LISA build/lisa-minix/minix-hd.image

# Phase 3 exit test: root on the ProFile. Boot 1 writes a file and runs
# fsck; boot 2, on the same image, reads the file back and runs fsck; then
# the file and the file system are checked on the Mac.
lisaem-hd: lisa-minix-hd
	cp build/lisa-minix/minix-hd.image build/lisa-minix/phase3-test.image
	$(PYTHON) tools/lisaem_run.py build/lisa-minix/phase3-test.image \
		--timeout 500 --char-delay 0.5 --log build/lisaem/phase3-boot1.log \
		--send 'login:=root' --send '# $$=ls /bin | wc' \
		--send '# $$=echo written on the Lisa > /tmp/hello; sync' \
		--send '# $$=fsck /dev/hd0' --until 'Free zones.*\n# $$'
	$(PYTHON) tools/lisaem_run.py build/lisa-minix/phase3-test.image \
		--timeout 500 --char-delay 0.5 --log build/lisaem/phase3-boot2.log \
		--send 'login:=root' --send '# $$=cat /tmp/hello' \
		--send '# $$=fsck /dev/hd0' --until 'Free zones.*\n# $$'
	test "$$($(PYTHON) tools/minixfs.py build/lisa-minix/phase3-test.image get /tmp/hello)" = "written on the Lisa"
	$(PYTHON) tools/minixfs.py build/lisa-minix/phase3-test.image check

# Phase 4 exit test: shell on the Lisa screen and keyboard (tests/lisaem-console.sh).
lisaem-console: lisa-minix-hd
	tests/lisaem-console.sh build/lisa-minix/minix-hd.image 1024
	tests/lisaem-console.sh build/lisa-minix/minix-hd.image 2048

# Sony floppy: mount, read and write a 400K disk (tests/lisaem-floppy.sh).
lisaem-floppy: lisa-minix-hd
	tests/lisaem-floppy.sh build/lisa-minix/minix-hd.image 1024

# Multi-file commands, elvis and mined included (tests/lisaem-cmds.sh).
lisaem-cmds: lisa-minix-hd
	tests/lisaem-cmds.sh build/lisa-minix/minix-hd.image 1024

ifeq ($(MACH),LISA)
$(B)/lisaboot.bin: boot/lisaboot.S
	@mkdir -p $(@D)
	$(CC) -c $(ASFLAGS) $< -o $(B)/lisaboot.o
	$(OBJCOPY) -O binary -j .text $(B)/lisaboot.o $@

# Root file system, loaded into the RAM disk by the boot block.
$(B)/root.fs: rootfs/lisa.proto $(CMDS) $(wildcard rootfs/etc/*) tools/mkfs.py
	sed 's|build/atari/|$(B)/|' rootfs/lisa.proto > $(B)/root.proto
	$(PYTHON) tools/mkfs.py $@ $(B)/root.proto

$(B)/minix.bin: $(B)/kernel.mix $(B)/mm.mix $(B)/fs.mix $(B)/init.mix $(B)/root.fs tools/build.py
	$(PYTHON) tools/build.py --lisa $(B)/kernel.mix $(B)/mm.mix $(B)/fs.mix \
		$(B)/init.mix $(B)/root.fs $@

$(B)/minix.image: $(B)/lisaboot.bin $(B)/minix.bin tools/mklisa.py
	$(PYTHON) tools/mklisa.py -f --entry 0x$$($(BINUTILS)nm $(B)/kernel.elf | \
		awk '$$3 == "start" {print $$1}') $(B)/lisaboot.bin $(B)/minix.bin $@

# Root file system on the ProFile (/dev/hd0): no RAM disk in the image.
# Multi-file commands in src/commands/<dir>: program, directory, objects,
# with the flags from each directory's own Makefile. Objects go to
# $(B)/mcmd/<dir> so the -D_POSIX_SOURCE of single-file commands does not
# apply to them.
MCMD_ar        = ar ar archiver rd_object wr_arhdr wr_object wr_ranlib
MCMD_bawk      = bawk bawk bawk bawkact bawkdo bawkpat bawksym
MCMD_de        = de de de de_stdin de_stdout de_diskio de_recover
MCMD_elvis     = elvis elvis blk cmd1 cmd2 curses cut ex input main misc modify \
		 move1 move2 move3 move4 move5 opts recycle redraw regexp regsub \
		 system tio tmp vars vcmd vi
MCMD_ctags     = ctags elvis ctags
MCMD_ref       = ref elvis ref
MCMD_virecover = virecover elvis virecover
MCMD_ic        = ic ic ic ic_input ic_output
MCMD_indent    = indent indent indent io lexi parse comment args
MCMD_kermit    = kermit kermit ckcmai ckucmd ckuusr ckuus2 ckuus3 ckcpro ckcfns \
		 ckcfn2 ckucon ckutio ckufio ckudia ckuscr
MCMD_m4        = m4 m4 main eval serv look misc expr
MCMD_make      = make make check input macro main make reader rules
MCMD_mdb       = mdb mdb mdb mdbexp mdbdis
MCMD_mined     = mined mined mined1 mined2
MCMD_nroff     = nroff nroff main command text io macros strings escape low
MCMD_patch     = patch patch patch pch inp util version
MCMD_rz        = rz zmodem rz
MCMD_sz        = sz zmodem sz
MCMDS = ar bawk de elvis ctags ref virecover ic indent kermit m4 make mdb \
	mined nroff patch rz sz

MDEFS_ar     = -I$(SRC)/commands/ar -DAAL
MDEFS_de     = -D_MINIX -D_POSIX_SOURCE
MDEFS_elvis  = -DCRUNCH
MDEFS_kermit = -DV7 -DMINIX
MDEFS_m4     = -DEXTENDED
MDEFS_mined  = -DUNIX		# the termcap version: uses all 40 lines
MDEFS_make   = -Dunix
MDEFS_mdb    = -DVOLATILE= -I$(SRC)
MDEFS_nroff  = -D_MINIX -D_POSIX_SOURCE
MDEFS_patch  = -DVOIDSIG -DCHARSPRINTF
MDEFS_zmodem = -DV7

$(B)/mcmd/%.o: $(SRC)/commands/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(MDEFS_$(firstword $(subst /, ,$*))) -I$(SRC)/commands/$(firstword $(subst /, ,$*)) $< -o $@

define MCMD_RULE
$(B)/cmd/$(word 1,$(MCMD_$(1))): $(CRTSO) $(patsubst %,$(B)/mcmd/$(word 2,$(MCMD_$(1)))/%.o,$(wordlist 3,99,$(MCMD_$(1)))) $(B)/libc.a
	@mkdir -p $$(@D)
	$$(LD) $$(LDFLAGS) -o $$@.elf $(CRTSO) $(patsubst %,$(B)/mcmd/$(word 2,$(MCMD_$(1)))/%.o,$(wordlist 3,99,$(MCMD_$(1)))) $(B)/libc.a $$(LIBGCC)
	$$(ELF2MNX) $$@.elf $$@
endef
$(foreach c,$(MCMDS),$(eval $(call MCMD_RULE,$(c))))

# All single-file commands in src/commands, plus sh.
ALL_CMDS = sh $(sort $(basename $(notdir $(wildcard $(SRC)/commands/*.c))))
SETUID_CMDS = mkdir rmdir su passwd

# Test programs for /usr/test: the Minix system call tests (src/test) and
# the Lisa port's own (tests/*.c).
TEST_PROGS = $(sort $(basename $(notdir $(wildcard $(SRC)/test/*.c)))) forkbench segv

# -O0: the tests were written for ACK, which did not optimize; with -O gcc
# turns loops that wait for a signal handler to change a variable (test1)
# into endless loops.
$(B)/test/%.o: DEFS = -D_MINIX -D_POSIX_SOURCE -O0
$(B)/test/%.o: $(SRC)/test/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEFS) $< -o $@
$(B)/test/%.o: tests/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEFS) $< -o $@
$(B)/test/%: $(CRTSO) $(B)/test/%.o $(B)/libc.a
	$(LD) $(LDFLAGS) -o $@.elf $(CRTSO) $(B)/test/$*.o $(B)/libc.a $(LIBGCC)
	$(ELF2MNX) $@.elf $@

$(B)/usr.fs: rootfs/lisa-hd.proto $(patsubst %,$(B)/cmd/%,$(ALL_CMDS) $(MCMDS)) $(patsubst %,$(B)/test/%,$(TEST_PROGS)) tests/phase5.sh tests/cmds.sh $(wildcard rootfs/etc/*) tools/mkfs.py
	$(PYTHON) -c 'import sys; \
		setuid = sys.argv[3].split(); \
		tests = sys.argv[4].split(); \
		lines = ["\t%-8s -%s-755 %s 2 %s/cmd/%s" % (c, "u" if c in setuid else "-", \
			"0" if c in setuid else "2", sys.argv[2], c) for c in sys.argv[5:]]; \
		tlines = ["\t\t%-9s ---755 2 2 %s/test/%s" % (t, sys.argv[2], t) for t in tests]; \
		sys.stdout.write(open(sys.argv[1]).read().replace("@BIN@", "\n".join(lines)) \
			.replace("@TEST@", "\n".join(tlines)))' \
		rootfs/lisa-hd.proto $(B) "$(SETUID_CMDS)" "$(TEST_PROGS)" $(sort $(ALL_CMDS) $(MCMDS)) > $(B)/hd.proto
	$(PYTHON) tools/mkfs.py $@ $(B)/hd.proto

$(B)/minix-hd.bin: $(B)/kernel.mix $(B)/mm.mix $(B)/fs.mix $(B)/init.mix tools/build.py
	$(PYTHON) tools/build.py --lisa $(B)/kernel.mix $(B)/mm.mix $(B)/fs.mix \
		$(B)/init.mix - $@

$(B)/minix-hd.image: $(B)/lisaboot.bin $(B)/minix-hd.bin $(B)/usr.fs tools/mklisa.py
	$(PYTHON) tools/mklisa.py -f --size 10 --fs $(B)/usr.fs \
		--entry 0x$$($(BINUTILS)nm $(B)/kernel.elf | awk '$$3 == "start" {print $$1}') \
		$(B)/lisaboot.bin $(B)/minix-hd.bin $@
endif

# ---- Lisa phase 1 test kernel -------------------------------------------------

L = build/lisa
LISA_CFLAGS = -mcpu=68000 -mshort -O -std=gnu89 -fno-builtin -w -nostdinc \
	      -I$(SRC)/kernel/lisa -MMD -MP

lisa: $(L)/lisatest.image

$(L)/lisaboot.bin: boot/lisaboot.S
	@mkdir -p $(@D)
	$(CC) -c $(ASFLAGS) $< -o $(L)/lisaboot.o
	$(OBJCOPY) -O binary -j .text $(L)/lisaboot.o $@
	@test $$(stat -f %z $@) -le 512 || { echo "boot block over 512 bytes"; exit 1; }

$(L)/%.o: $(SRC)/kernel/lisa/%.c
	@mkdir -p $(@D)
	$(CC) -c $(LISA_CFLAGS) $< -o $@

$(L)/%.o: $(SRC)/kernel/lisa/%.S
	@mkdir -p $(@D)
	$(CC) -c $(ASFLAGS) $< -o $@

# Phase 1 test kernel (PLAN.md): lisastart.o first, for the vectors at 0.
$(L)/lisatest.elf: $(L)/lisastart.o $(L)/lisatest.o toolchain/lisa-test.ld
	$(LD) --no-warn-rwx-segments -T toolchain/lisa-test.ld -o $@ \
		$(L)/lisastart.o $(L)/lisatest.o $(LIBGCC)

$(L)/lisatest.image: $(L)/lisaboot.bin $(L)/lisatest.elf tools/mklisa.py
	$(OBJCOPY) -O binary $(L)/lisatest.elf $(L)/lisatest.bin
	$(PYTHON) tools/mklisa.py -f \
		--entry 0x$$($(BINUTILS)nm $(L)/lisatest.elf | awk '$$3 == "start" {print $$1}') \
		$(L)/lisaboot.bin $(L)/lisatest.bin $@

lisaem-test: $(L)/lisatest.image
	$(PYTHON) tools/lisaem_run.py $< --timeout 120 --until 'halted'

# ---- pattern rules ----------------------------------------------------------

$(B)/%.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEFS) $< -o $@

$(B)/%.o: $(SRC)/%.s
	@mkdir -p $(@D)
	$(CC) -c $(ASFLAGS) $(DEFS) $< -o $@

clean:
	rm -rf build

-include $(shell find $(B) -name '*.d' 2>/dev/null)

.PHONY: all clean hatari-boot hatari-shell lisa lisaem-test lisa-minix lisaem-minix \
	lisa-minix-hd lisaem-hd lisaem-console lisaem-cmds lisaem-floppy
.SECONDARY:
